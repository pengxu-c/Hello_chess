// ============================================================================
// bench_match.h - 自动对弈执行与统计（纯文本，无 UI / 无网络依赖）
//
// 用途定位：给开发者与 agent 做「棋力 / 正确性回归」的命令行工具。
// 职责刻意保持窄 —— 本层只关心怎么把一盘棋下完、怎么统计，不含算法知识；
// 棋手统一由 gomoku::IPlayer 插件产出（与产品运行的是同一套插件）。
//
// 【为什么用契约层的 ViewState 而不是旧 Board】
//   这样 bench 与产品共用同一条"玩家插件"路径：bench 里跑得好的棋手，
//   在网页/终端里就是同一个对象；反之亦然。旧版 bench 自己维护一套
//   编号 → new 棋手的映射，正是"新增玩家要改多处"的来源之一。
// ============================================================================
#pragma once

#include "../contracts/GameTypes.h"
#include "../contracts/IPlayer.h"

#include <memory>
#include <string>
#include <vector>

namespace gomoku {

// ---- 可参与 bench 的玩家插件 id（与注册表一致）----
// human：需要交互，不入 bench；api-ai：依赖网络且不可复现，同样不入。
std::vector<std::string> benchPlayerIds();
std::string benchPlayerName(const std::string& id);
bool isBenchPlayable(const std::string& id);

// ---- 对局配置 ----
struct MatchConfig {
    std::string p1 = "tactical-max";   // 先手（黑）
    std::string p2 = "minimax";        // 后手（白）
    int      games      = 10;
    int      boardSize  = 15;
    int      winLen     = 5;
    int      budgetMs   = 1500;        // 单步思考预算（毫秒）
    bool     fixedOpening = true;      // 首手固定天元 → 结果可复现
    unsigned seed       = 20260928;
};

// ---- 单局 / 汇总 ----
struct GameRecord {
    Stone     winner = Stone::Empty;   // Empty = 和棋
    int       moveCount = 0;
    double    blackTotalMs = 0.0;
    double    whiteTotalMs = 0.0;
    double    blackMaxMs   = 0.0;
    double    whiteMaxMs   = 0.0;
    bool      aborted  = false;        // AI 给出无效着法等异常
    std::string note;
    std::vector<Coord> moves;          // 完整棋谱（便于复盘）
};

struct MatchSummary {
    int    games   = 0;
    int    p1Wins  = 0;
    int    p2Wins  = 0;
    int    draws   = 0;
    int    aborted = 0;
    double winRate  = 0.0;      // p1 得分率（和棋计 0.5）
    double avgMoves = 0.0;
    double p1AvgMs  = 0.0;
    double p2AvgMs  = 0.0;
    double p1MaxMs  = 0.0;
    double p2MaxMs  = 0.0;
};

GameRecord   playOneGame(const MatchConfig& cfg, IPlayer& p1, IPlayer& p2);
MatchSummary runMatch(const MatchConfig& cfg, bool verbose);

}  // namespace gomoku
