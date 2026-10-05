// ============================================================
// ai_config.h - AI 配置声明
// 由 config.json 驱动，字段可扩展，支持切换任意 OpenAI 兼容提供商
// ============================================================
#pragma once
#include <string>

// API 玩家配置结构体：全部字段由 JSON 映射，新增字段可平滑扩展
struct AIConfig {
    bool   enabled = false;             // 配置是否完整可用（apiUrl/apiKey/model 缺一即 false）
    std::string displayName;            // 控制台显示的 AI 名称（留空则回退为 model）
    std::string provider;               // 提供商标识（openai/deepseek/ollama...，仅展示用途）
    std::string apiUrl;                 // API 端点地址，必填
    std::string apiKey;                 // API 密钥，必填（含 API 密钥，config.json 已入 .gitignore）
    std::string model;                  // 模型名，必填；切换模型只改这一行
    double temperature = 0.3;           // 生成温度 0.0~2.0
    int    maxTokens   = 50;            // 最大输出 token 数
    std::string systemPrompt;           // 系统提示词，支持 {size}/{win_len}/{color} 变量
    std::string userPromptTemplate;     // 用户提示模板，支持 {board}/{color} 变量

    bool loadFromFile(const std::string& path = "config.json");  // 从 JSON 文件加载
};