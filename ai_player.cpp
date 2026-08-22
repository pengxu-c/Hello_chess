// ============================================================
// ai_player.cpp - API 玩家实现：HTTP 请求 + JSON 解析
// ============================================================
#include "ai_player.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <sstream>
#include <algorithm>

// 在 str 中将所有 key 替换为 value（消除 callAPI 中分散的 while-find-replace 代码）
// file-local static：仅本翻译单元可见，无需暴露到头文件
static void replaceTemplate(std::string& str, const std::string& key, const std::string& value) {
    size_t p = 0;
    while ((p = str.find(key, p)) != std::string::npos) {
        str.replace(p, key.size(), value);
        p += value.size();
    }
}

APIPlayer::APIPlayer(const AIConfig& cfg) : cfg_(cfg) {
    nameBuf_ = cfg_.displayName;
}

bool APIPlayer::isHuman() const { return false; }
bool APIPlayer::needsDelay() const { return true; }
const char* APIPlayer::name() const { return nameBuf_.c_str(); }

// 棋盘序列化为文本：B=黑/W=白/.=空，逐行空格分隔，供大模型读取局面。
std::string APIPlayer::boardToString(const Board& board) const {
    std::ostringstream oss;
    int n = board.size();
    for (int r = 0; r < n; r++) {
        if (r > 0) oss << '\n';
        for (int c = 0; c < n; c++) {
            if (c > 0) oss << ' ';
            ChessType t = board.at(r, c);
            if      (t == ChessType::Black) oss << 'B';
            else if (t == ChessType::White) oss << 'W';
            else                            oss << '.';
        }
    }
    return oss.str();
}

// 从模型回复文本中解析坐标：找首个逗号，向前/向后收集数字作为行/列，越界返回无效。
// 容错设计：能匹配 "7,8"、"(7,8)"、"row 7, col 8" 等多种格式。
// boardSize 用于边界检查（替代原全局棋盘尺寸变量）
Pos APIPlayer::parseResponse(const std::string& text, int boardSize) const {
    auto comma = std::find(text.begin(), text.end(), ',');
    if (comma != text.end()) {
        std::string rStr, cStr;
        auto it = comma;
        while (it != text.begin()) {
            --it;
            if (*it >= '0' && *it <= '9') rStr = *it + rStr;
            else if (!rStr.empty()) break;
        }
        it = comma;
        while (++it != text.end()) {
            if (*it >= '0' && *it <= '9') cStr += *it;
            else if (!cStr.empty()) break;
        }
        if (!rStr.empty() && !cStr.empty()) {
            int r = std::stoi(rStr);
            int c = std::stoi(cStr);
            if (r >= 0 && r < boardSize && c >= 0 && c < boardSize) return {r, c};
        }
    }
    return {-1, -1};
}

// curl 回调：把响应体追加进 std::string 缓冲区
size_t APIPlayer::writeCallback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// 发起 OpenAI 兼容的 chat/completions 请求，成功返回原始 JSON 响应文本
// boardSize/winLen 用于提示词模板变量替换（替代原全局棋盘尺寸/连珠数变量）
std::string APIPlayer::callAPI(const std::string& boardStr, ChessType color, int boardSize, int winLen) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";   // curl 初始化失败

    // 用配置的 system/user 提示词拼接请求消息
    std::string sysPrompt = cfg_.systemPrompt;
    std::string userPrompt = cfg_.userPromptTemplate;
    {
        // 模板变量替换（尺寸/连珠数由参数传入，不再读全局）
        std::string sz = std::to_string(boardSize);
        std::string wl = std::to_string(winLen);
        std::string c  = (color == ChessType::Black) ? "Black" : "White";
        // 统一调用 replaceTemplate 替换所有模板变量（含 {board}），消除分散的 while-find-replace
        replaceTemplate(sysPrompt,   "{size}",    sz);
        replaceTemplate(sysPrompt,   "{win_len}", wl);
        replaceTemplate(sysPrompt,   "{color}",   c);
        replaceTemplate(userPrompt,  "{size}",    sz);
        replaceTemplate(userPrompt,  "{win_len}", wl);
        replaceTemplate(userPrompt,  "{color}",   c);
        replaceTemplate(userPrompt,  "{board}",   boardStr);
    }

    nlohmann::json body;
    body["model"] = cfg_.model;
    body["temperature"] = cfg_.temperature;
    body["max_tokens"] = cfg_.maxTokens;
    body["messages"] = nlohmann::json::array({
        {{"role", "system"}, {"content", sysPrompt}},
        {{"role", "user"},   {"content", userPrompt}}
    });
    std::string bodyStr = body.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + cfg_.apiKey;
    headers = curl_slist_append(headers, auth.c_str());

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, cfg_.apiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodyStr.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        printf(">> APIPlayer: curl error: %s\n", curl_easy_strerror(res));
        return "";
    }
    return response;
}

Pos APIPlayer::place(Board& board, ChessType color) {
    if (!cfg_.enabled) return {-1, -1};   // 未配置完整则直接无效，上一帧重试

    // 1. 棋盘序列化 → 2. 请求 API → 3. 解析响应坐标
    std::string boardStr = boardToString(board);
    std::string response = callAPI(boardStr, color, board.size(), board.winLen());
    if (response.empty()) return {-1, -1};

    try {
        nlohmann::json j = nlohmann::json::parse(response);
        // OpenAI 兼容响应格式: choices[0].message.content
        std::string content = j.at("choices").at(0).at("message").at("content");
        Pos p = parseResponse(content, board.size());
        if (p.valid() && board.inBounds(p.r, p.c) && board.at(p.r, p.c) == ChessType::None) {
            failCount_ = 0;    // 一次成功即可归零
            printf(">> APIPlayer(%s) -> (%d,%d)\n", cfg_.displayName.c_str(), p.r, p.c);
            return p;
        }
    } catch (const std::exception& e) {
        printf(">> APIPlayer: JSON parse error: %s\n", e.what());
    }
    // 连续失败防护：达到阈值后改用本地兜底，避免主循环无休止重试
    if (++failCount_ >= kMaxFails) {
        printf(">> APIPlayer: %d consecutive failures, using fallback move.\n", kMaxFails);
        failCount_ = 0;
        return fallbackMove(board);
    }
    return {-1, -1};   // 解析失败返回无效位置，主循环将重试
}

// 兜底着法：优先中心，其次按螺旋找靠近中心的首个空位
Pos APIPlayer::fallbackMove(const Board& board) const {
    int n = board.size();
    int cr = n / 2, cc = n / 2;
    if (board.at(cr, cc) == ChessType::None) return {cr, cc};
    for (int radius = 1; radius <= n; radius++) {
        for (int r = 0; r < n; r++) {
            for (int c = 0; c < n; c++) {
                if (board.at(r, c) != ChessType::None) continue;
                if (abs(r - cr) <= radius && abs(c - cc) <= radius) return {r, c};
            }
        }
    }
    return {-1, -1};
}