// ============================================================
// ai_player.h - API 玩家类声明
// 通过 HTTP API 调用远程大模型落子，支持 OpenAI 兼容接口
// 基于 libcurl + nlohmann/json，配置由 AIConfig 驱动
// ============================================================
#pragma once
#include "player.h"
#include "ai_config.h"

class APIPlayer : public Player {
public:
    explicit APIPlayer(const AIConfig& cfg);
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
private:
    AIConfig cfg_;
    std::string nameBuf_;   // 持有 name() 返回的字符串内存

    // 连续失败防护：API 持续异常时暂不无限重试，改用本地兜底着法
    static constexpr int kMaxFails = 3;
    int failCount_ = 0;

    std::string boardToString(const Board& board) const;
    Pos parseResponse(const std::string& text) const;
    Pos fallbackMove(const Board& board) const;                 // 兜底：选中心附近空位

    static size_t writeCallback(void* ptr, size_t size, size_t nmemb, void* userdata);
    std::string callAPI(const std::string& boardStr, ChessType color);
};