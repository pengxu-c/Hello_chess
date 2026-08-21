// ============================================================
// player.h - 棋手类声明
// 抽象基类 Player，派生：HumanPlayer、GreedyScoringAI(多档位)、MinimaxPP、APIPlayer
// 每个棋手实现 place() 返回落子位置
// 评分系统：单一评分核 segValue(几何级数梯度,对任意WIN_LEN成立) + 单点核 pointScore(攻防同函数)
// 各 AI 攻防权重：EasyJudge=防守, PG1.0=防守, PG1.1=防守+0.9*进攻, Minimax++=搜索主导+0.001启发式
// Minimax++ 增强：alpha-beta 剪枝 + 启发式排序 + Zobrist 置换表 + 静态缓冲 + 统一评分
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
    virtual Pos place(Board& board, ChessType color) = 0;  // 返回落子位置（无效表示本帧不落子）
    virtual bool isHuman() const = 0;                       // 是否为人类
    virtual bool needsDelay() const { return true; }        // 是否需要思考延迟（AI 默认需要）
    virtual const char* name() const = 0;                   // 棋手名称

    // ---- 最后一手跟踪（供 UI 闪烁标记） ----
    void markLastMove(const Pos& p) { lastMove_ = p; }      // 记录该玩家最近一手
    const Pos& lastMove() const { return lastMove_; }       // 查询最近一手（无效表示尚无）
protected:
    Pos lastMove_{ -1, -1 };                                // 该玩家最近落子位置
};

// ---- 人类玩家：从 UI 鼠标点击获取落子 ----
class HumanPlayer : public Player {
public:
    explicit HumanPlayer(UI& ui);
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
private:
    UI& ui_;
};

// ---- 通用评分 AI：单一实现，攻防权重 + 显示名参数化（可扩展任意难度档） ----
class GreedyScoringAI : public Player {
public:
    GreedyScoringAI(double attackWeight, const char* displayName);
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
private:
    double attackWeight_ = 0.0;     // 进攻权重：0=纯防守，>0 加入进攻考量
    std::string name_;              // 显示名称（区分各档位）
};

// ---- Minimax++：极小极大 + alpha-beta 剪枝 + 启发式排序 + Zobrist 置换表 ----
class MinimaxPP : public Player {
public:
    explicit MinimaxPP(Judge& judge);
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
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
