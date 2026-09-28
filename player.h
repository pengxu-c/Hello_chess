// ============================================================
// player.h - 棋手类声明
// 抽象基类 Player，派生：HumanPlayer、EasyJudgeAI、GreedyScoringAI(多档位)、MinimaxPP、TacticalMax、APIPlayer
//
// 【模板方法模式】（本次升级引入）
//   Player::place() 是非虚的统一入口，负责：
//     1) 空盘且允许自动首手时 → 走基类 openingMove()（中心正方形真随机）
//     2) 其余情况 → 委托派生类的 chooseMove() 做具体决策
//     3) 统一维护 lastMove_（供 UI 标记最后一手）
//   派生类因此只需实现 chooseMove()，首手随机规则在全项目只有一份实现，
//   新增棋手时自动继承该行为 —— 这是"最高可扩展性"的落点。
//
// 评分系统：单一评分核 segValue + 单点核 pointScore（供贪心/启发式复用）
// 必胜/必防威胁检测统一由 threat.h/.cpp 的 ThreatDetector 提供
// 各 AI 攻防权重：EasyJudge=防守, PG1.0=防守, PG1.1=防守+进攻, Minimax++=搜索主导
// ============================================================
#pragma once
#include "core.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

class UI;
class Judge;
class Stats;

// ---- 棋手抽象基类 ----
class Player {
public:
    static bool randomEnabled;                                 // 随机机制开关（默认 false）
    virtual ~Player() = default;

    // 非虚统一入口（模板方法）：首手随机 → 否则委托 chooseMove。
    // 返回值无效（r<0）表示本帧不落子（如人类尚未点击、API 暂未响应）。
    Pos place(Board& board, ChessType color) {
        Pos p = (autoFirstMove() && board.isEmpty()) ? openingMove(board)
                                                     : chooseMove(board, color);
        if (p.valid()) lastMove_ = p;
        return p;
    }

    virtual bool isHuman() const = 0;                       // 是否为人类
    virtual bool needsDelay() const { return true; }        // 是否需要思考延迟（AI 默认需要）
    virtual const char* name() const = 0;                   // 棋手名称

    // ---- 最后一手跟踪（供 UI 闪烁标记） ----
    void markLastMove(const Pos& p) { lastMove_ = p; }      // 记录该玩家最近一手
    const Pos& lastMove() const { return lastMove_; }       // 查询最近一手（无效表示尚无）

protected:
    // 派生类唯一需要实现的"思考"逻辑
    virtual Pos chooseMove(Board& board, ChessType color) = 0;

    // 是否允许基类自动处理首手：默认「非人类」即为真。
    // 人类玩家（自己点）与 APIPlayer（由大模型自决）重写为 false。
    virtual bool autoFirstMove() const { return !isHuman(); }

    // 中心正方形真随机首手：在棋盘中心、边长 n/3+1 的正方形内均匀随机选一个空位。
    // 种子取「高精度时钟 ^ 硬件熵」，保证每盘开局不同且不可预测。
    Pos openingMove(const Board& board) const;

    Pos lastMove_{ -1, -1 };                                // 该玩家最近落子位置
};

// ---- 人类玩家：从 UI 鼠标点击获取落子 ----
class HumanPlayer : public Player {
public:
    explicit HumanPlayer(UI& ui);
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
protected:
    Pos chooseMove(Board& board, ChessType color) override;
    bool autoFirstMove() const override { return false; }   // 人类自己下，不走首手随机
private:
    UI& ui_;
};

// ---- 最简 AI：随机 + 堵（调用 ThreatDetector 拿必胜/必防候选，随机下）----
// 定位：最弱、行为不可预测的陪练档。不做评分、不做搜索。
class EasyJudgeAI : public Player {
public:
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
protected:
    Pos chooseMove(Board& board, ChessType color) override;
};

// ---- 通用评分 AI：单一实现，攻防权重 + 显示名参数化（可扩展任意难度档） ----
class GreedyScoringAI : public Player {
public:
    GreedyScoringAI(double attackWeight, const char* displayName);
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
protected:
    Pos chooseMove(Board& board, ChessType color) override;
private:
    double attackWeight_ = 0.0;     // 进攻权重：0=纯防守，>0 加入进攻考量
    std::string name_;              // 显示名称（区分各档位）
};

// ---- Minimax++：极小极大 + alpha-beta 剪枝 + 启发式排序 + Zobrist 置换表 ----
class MinimaxPP : public Player {
public:
    explicit MinimaxPP(Judge& judge);
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
protected:
    Pos chooseMove(Board& board, ChessType color) override;
private:
    Judge& judge_;
    static constexpr int kDepth = 4;      // 搜索深度（配合排序+哈希可加深）
    static constexpr int kRadius = 2;     // 候选着法半径（易调）
    static constexpr int kInf = 100000000;
    static constexpr int kMaxBoard = 30;  // 最大棋盘尺寸（Zobrist/静态缓冲用）
    // 局面评估：沿 4 方向扫描连续同色线段，统一调用 segValue 评分（与 pointScore 同表）
    int evaluate(const Board& board, ChessType aiColor) const;
    // alpha-beta 递归搜索；hash 为当前局面的 Zobrist 哈希，用于置换表查询/存储
    int minimax(Board& board, int depth, int alpha, int beta,
                ChessType curColor, bool isMax, ChessType aiColor, uint64_t hash);
    std::vector<Pos> generateMoves(const Board& board) const;            // 生成候选着法(已有棋子周围 kRadius 内空位)
    // Zobrist 哈希与置换表
    uint64_t zobrist_[kMaxBoard][kMaxBoard][2];                          // [r][c][0=Black,1=White]
    struct TTEntry { int depth; int value; int flag; };                  // flag: 0=exact, 1=lower bound, 2=upper bound
    std::unordered_map<uint64_t, TTEntry> transTable_;
    bool zobristInited_ = false;
    void initZobrist();                                                  // 初始化 Zobrist 随机数表
    uint64_t boardHash(const Board& board) const;                        // 计算当前棋盘的 Zobrist 哈希
};
