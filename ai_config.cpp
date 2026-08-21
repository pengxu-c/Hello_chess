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
bool AIConfig::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        printf(">> AIConfig: config.json not found, API player disabled.\n");
        enabled = false;
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
        return true;
    } catch (const std::exception& e) {
        printf(">> AIConfig: parse error: %s\n", e.what());
        enabled = false;
        return false;
    }
}