// ============================================================================
// bench_match.h - 自动对弈执行与统计（纯文本，无 UI / 无网络依赖）
//
// 用途定位：给开发者与 agent 做「棋力 / 正确性回归」的命令行工具。
// 职责刻意保持窄 —— 本层只关心怎么把一盘棋下完、怎么统计，
// 不含任何算法知识；棋手交由 createPlayerById 统一产出。
//
// 支持任意棋盘尺寸 n 与连珠数 w，适配所有 n 子棋规则。
// ============================================================================
#pragma once

#include "../core.h"

#include <memory>
#include <string>
#include <vector>

class Player;
class Judge;
class UI;

// ---- 可参与 bench 的本地棋手编号（与 main.cpp 保持一致）----
// 6 号（远程大模型 API）不入 bench：依赖网络且不可复现。
// 1 号是人类（需要交互），同样不入。
enum BenchPlayerId {
    kPidEasyJudge    = 2,   // 随机 + 堵
    kPidPureGreed10  = 3,   // 纯防守评分
    kPidPureGreed11  = 4,   // 攻防评分
    kPidMinimaxPP    = 5,   // α-β 搜索
    kPidTacticalMax  = 7,   // 增量评估 + PVS + VCF/VCT（最强）
};

const char* playerIdName(int id);                       // 显示名
const std::vector<int>& benchPlayerIds();               // 全部可用编号
bool isPlayableId(int id);                              // 是否可参与自动对弈

// 棋手工厂（ui 传 nullptr 即可，bench 不构造人类棋手）
std::unique_ptr<Player> createPlayerById(int id, Judge& judge, UI* ui);

// ---------------- 对局配置 ----------------
struct MatchConfig {
    int      p1 = kPidTacticalMax;   // 先手（黑）
    int      p2 = kPidMinimaxPP;     // 后手（白）
    int      games      = 10;
    int      boardSize  = 15;
    int      winLen     = 5;
    int      budgetMs   = 1500;      // 单步思考预算（毫秒）
    bool     fixedOpening = true;    // 首手固定天元 → 结果可复现
    unsigned seed       = 20260928;
};

// ---------------- 单局 / 汇总 ----------------
struct GameRecord {
    ChessType winner   = ChessType::None;   // None = 和棋
    int       moveCount = 0;
    double    blackTotalMs = 0.0;
    double    whiteTotalMs = 0.0;
    double    blackMaxMs   = 0.0;
    double    whiteMaxMs   = 0.0;
    bool      aborted  = false;             // AI 给出无效着法等异常
    std::string note;
    std::vector<Pos> moves;                 // 完整棋谱（便于复盘） 
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

GameRecord   playOneGame(const MatchConfig& cfg, Player& p1, Player& p2);
MatchSummary runMatch(const MatchConfig& cfg, bool verbose);
