// ============================================================
// player.h - 棋手类声明
// 抽象基类 Player，派生：HumanPlayer、EasyJudgeAI、PureGreed10、PureGreed11
// 每个棋手实现 place() 返回落子位置
// ============================================================
#pragma once
#include "core.h"

class UI;
class Judge;
class Stats;

// ---- 棋手抽象基类 ----
class Player {
public:
    virtual ~Player() = default;
    virtual Pos place(Board& board, ChessType color) = 0;  // 返回落子位置（无效表示本帧不落子）
    virtual bool isHuman() const = 0;                       // 是否为人类
    virtual const char* name() const = 0;                   // 棋手名称
};

// ---- 人类玩家：从 UI 鼠标点击获取落子 ----
class HumanPlayer : public Player {
public:
    explicit HumanPlayer(UI& ui);
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
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
    bool canBlockFive(Board& board, int r, int c, ChessType oppColor);  // 检测落子能否阻挡对方五连
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

// ---- PureGreed 1.1：攻防评分 AI（最强） ----
class PureGreed11 : public Player {
public:
    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    const char* name() const override;
};
