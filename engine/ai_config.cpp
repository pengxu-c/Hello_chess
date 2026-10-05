// ============================================================
// ai_config.cpp - AI 配置实现：从 JSON 文件加载
// config.json 为开箱即用模板，用户只需补填 api_key/model/名字
// ============================================================
#include "ai_config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

// 加载 config.json 中的 "ai_player" 节点到本结构体。
// 缺省字段采用空值/保守默认值；apiUrl/model/apiKey 任一为空即视为未启用。
//
// 说明：本函数会被"玩家目录探测"多次调用（界面每次拉目录都要知道 API 玩家
// 是否可用）。因此按路径缓存一次结果：同名文件只打印一次日志，
// 避免每次打开网页都刷屏。文件内容在进程内视为不变（配置只在启动时读取）。
bool AIConfig::loadFromFile(const std::string& path) {
    static std::string cachedPath;
    static bool cached = false;
    static AIConfig cachedConfig;
    if (cached && path == cachedPath) {
        *this = cachedConfig;
        return true;
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        printf(">> AIConfig: config.json not found, API player disabled.\n");
        enabled = false;
        cachedPath = path;
        cachedConfig = *this;
        cached = true;
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        auto& ai = j.at("ai_player");
        displayName        = ai.value("display_name", "");
        provider           = ai.value("provider", "");
        apiUrl             = ai.value("api_url", "");
        apiKey             = ai.value("api_key", "");
        model              = ai.value("model", "");
        temperature        = ai.value("temperature", 0.3);
        maxTokens          = ai.value("max_tokens", 50);
        systemPrompt       = ai.value("system_prompt", "");
        userPromptTemplate = ai.value("user_prompt_template", "");
        // 三者缺一不可：地址、密钥、模型名都必须由用户正确填写
        enabled = !apiUrl.empty() && !apiKey.empty() && !model.empty() &&
                  apiKey != "YOUR_API_KEY_HERE";
        if (displayName.empty()) displayName = model;   // 未填显示名时回退为模型名
        if (enabled) {
            printf(">> AIConfig loaded: model=%s, provider=%s\n", model.c_str(), provider.c_str());
        } else {
            printf(">> AIConfig: missing api_url/api_key/model. API player falls back to Minimax++.\n");
        }
        cachedPath = path;
        cachedConfig = *this;
        cached = true;
        return true;
    } catch (const std::exception& e) {
        printf(">> AIConfig: parse error: %s\n", e.what());
        enabled = false;
        return false;
    }
}