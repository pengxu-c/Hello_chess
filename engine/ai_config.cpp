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
// 是否可用）。因此按路径缓存结果：同一路径只读一次、只报一次日志。
// 注意缓存是按"路径"而不是按"文件名"做的 —— 调用方会依次试多个候选路径，
// 每个候选各报一条 not found 是预期行为，不是刷屏。
// 文件内容在进程内视为不变（配置只在启动时读取）。
bool AIConfig::loadFromFile(const std::string& path) {
    static std::string cachedPath;
    static bool cached = false;
    static bool cachedOk = false;
    static AIConfig cachedConfig;
    if (cached && path == cachedPath) {
        *this = cachedConfig;
        // 【返回值必须一起缓存】旧实现无论上次成功与否都 return true，
        // 于是"文件不存在"这个结果被缓存成"加载成功"，调用方只能靠
        // cfg.enabled 兜底，日志与真实结果的语义就此分家。
        return cachedOk;
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        // 【必须打印实际路径】只写 "config.json not found" 时，
        // 从 build/Release 启动根本看不出它到底在哪个目录找。
        // 调用方会依次试 3 个候选，这里各报一条、各带路径，才能定位。
        printf(">> AIConfig: not found: %s\n", path.c_str());
        enabled = false;
        cachedPath = path;
        cachedConfig = *this;
        cachedOk = false;
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
        cachedOk = true;
        cached = true;
        return true;
    } catch (const std::exception& e) {
        printf(">> AIConfig: parse error in %s: %s\n", path.c_str(), e.what());
        enabled = false;
        // 坏文件同样缓存：否则每次构建玩家目录都要重新解析、重复报错
        cachedPath = path;
        cachedConfig = *this;
        cachedOk = false;
        cached = true;
        return false;
    }
}