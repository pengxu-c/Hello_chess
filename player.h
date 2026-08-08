// ============================================================
// player.h - 棋手类声明
// 抽象基类 Player，派生：HumanPlayer、EasyJudgeAI、PureGreed10、PureGreed11、MinimaxPP
// 每个棋手实现 place() 返回落子位置
// ============================================================
#pragma once
#include "core.h"
#include <vector>

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

// ---- EasyJudge：超简单 AI，随机落子 + 防输机制 ----
class EasyJudgeAI : public Player {
public:
    EasyJudgeAI(Judge& judge, Stats& stats);
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    const char* name() const override;
private:

    Judge& judge_;
    Stats& stats_;
};

// ---- PureGreed 1.0：纯防守评分 AI ----
class PureGreed10 : public Player {
public:
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    const char* name() const override;
};

// ---- PureGreed 1.1：攻防评分 AI ----
class PureGreed11 : public Player {
public:
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    const char* name() const override;
};

// ---- Minimax++：极小极大 + alpha-beta 剪枝搜索 AI ----
class MinimaxPP : public Player {
public:
    explicit MinimaxPP(Judge& judge);
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;
private:
    Judge& judge_;
    static constexpr int kDepth = 3;      // 搜索深度（易调）
    static constexpr int kRadius = 2;     // 候选着法半径（易调）
    static constexpr int kInf = 100000000;
    // 局面评估：沿 4 方向扫描连续同色线段，按长度+两端开放数评分(活/眠/死棋)
    int evaluate(const Board& board, ChessType aiColor) const;
    int minimax(Board& board, int depth, int alpha, int beta,
                ChessType curColor, bool isMax, ChessType aiColor);      // alpha-beta 递归搜索
    std::vector<Pos> generateMoves(const Board& board) const;            // 生成候选着法(已有棋子周围 kRadius 内空位)
};
