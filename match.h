// ============================================================
// match.h - 一局对战的规则内核（微内核的"最小内核"）
//
// 设计原则：内核最小化。本类只负责"回合状态 + 落子判定"，
// 不涉及玩家、界面、存储、网络、线程 —— 这些全部是外围插件。
//
// 谁在用它：
//   CLI（GameController）与 Web（SessionController）共享同一套
//   落子/判胜/换手规则，消除此前各自手写的重复回合逻辑。
//
// 线程模型：本类不含任何锁。调用方需保证同一实例只被单线程驱动
//   （Web 侧由 loop 线程独占，HTTP 线程只投递请求）。
// ============================================================
#pragma once
#include "core.h"
#include <vector>

class Match {
public:
    // 持有棋盘与裁判的引用（不拥有，生命周期由调用方保证）
    Match(Board& board, Judge& judge);

    void reset();                                  // 复位回合状态（调用前请自行清空棋盘）

    ChessType turn() const { return turn_; }        // 当前该走的一方
    bool      over() const { return over_; }        // 是否已终局
    bool      isDraw() const { return over_ && winner_ == ChessType::None; }
    ChessType winner() const { return winner_; }    // 终局胜方（和棋为 None）
    int       moveCount() const { return moveCount_; }

    // 在 (r,c) 落当前回合一子：成功返回 true（随后可能终局或换手）；
    // 越界 / 占用 / 已终局时返回 false，不改变任何状态。
    bool applyMove(int r, int c);

    // 撤销最近一手（悔棋重建用）：撤掉 (r,c) 并回退回合/终局状态。
    // 调用方需保证 (r,c) 确实是最近一手。
    void undoLastMove(int r, int c);

    // 按剩余步数重新对齐回合（外部重建棋盘后调用）：
    // 黑先手，偶数步轮到黑。用于"从存储重放"后同步内核状态。
    void syncMoveCount(int count);

    // 直接设置当前回合（悔棋"人类优先"等外部规则同步用）。
    void setTurn(ChessType t) { turn_ = t; }

private:
    Board& board_;
    Judge& judge_;
    ChessType turn_ = ChessType::Black;
    bool      over_ = false;
    ChessType winner_ = ChessType::None;
    int       moveCount_ = 0;
    std::vector<Pos> history_;   // 落子顺序（供撤销与重建）
};