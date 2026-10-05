// ============================================================
// kernel/MatchState.h - 一局棋的纯状态机（微内核的「最小内核」）
//
// 【边界】本类只知道：棋盘、规则、回合、胜负、落子历史。
// 它不知道玩家是谁、不知道界面在哪、不知道有没有存储、
// 不知道是不是多线程 —— 因此它可以被任何宿主复用，
// 也可以脱离宿主单独做单元测试。
//
// 线程模型：不含任何锁。调用方保证同一实例只被单线程驱动
// （本项目由 MatchSession 的控制线程独占）。
// ============================================================
#pragma once
#include "GomokuRules.h"
#include "IBoard.h"
#include "../contracts/GameTypes.h"
#include <vector>

namespace gomoku {

class MatchState {
public:
    MatchState(IBoard& board, const IRules& rules);

    // ---- 开新局：清空棋盘、重置回合与历史 ----
    void reset(const RulesConfig& rules);

    // ---- 状态查询 ----
    const IBoard& board() const { return board_; }
    IBoard&       board() { return board_; }
    const RulesConfig& rules() const { return cfg_; }
    Stone turn() const { return turn_; }
    bool over() const { return over_; }
    Stone winner() const { return winner_; }        // over 且 winner==Empty 表示和棋
    int moveCount() const { return static_cast<int>(history_.size()); }
    const std::vector<Move>& history() const { return history_; }
    // 本进程内第几局（每次 reset 递增）。界面用它判断"是否换了一局"。
    long long matchId() const { return matchId_; }

    // ---- 推进 ----
    // 在 (r,c) 落当前回合方一子。越界/占用/已终局一律返回 false 且不改变任何状态。
    // 返回值只表示「这步被接受」，胜负请查 over()/winner()。
    bool applyMove(int r, int c);

    // ---- 悔棋 ----
    // 撤掉最近 n 手并以历史重建棋盘（重建保证任何棋盘实现都正确，
    // 且不要求玩家实现提供撤销能力）。返回实际撤销手数。
    int undo(int n);

    // ---- 外部重建 ----
    // 用一份完整历史重建（载入残局/回放定位用）：棋盘清空后按顺序重放，
    // 回合与终局状态按第 lastMove 手的结果推导。若 lastMove < 0 表示全部重放。
    void rebuild(const std::vector<Move>& moves, int upTo = -1);

    // ---- 座位统计（供界面显示与存档） ----
    int movesOf(Stone s) const { return (s == Stone::Black) ? blackMoves_ : whiteMoves_; }

    // 直接指定当前回合方（仅用于外部人性化规则同步，如"悔棋后把回合交还人类"）。
    // 不会改动棋盘与历史，也不会清除终局标记 —— 调用方需自己保证语义正确。
    void setTurn(Stone t) { turn_ = t; }

private:
    IBoard& board_;
    const IRules& rules_;
    RulesConfig cfg_{};
    Stone turn_ = Stone::Black;
    bool over_ = false;
    Stone winner_ = Stone::Empty;
    int blackMoves_ = 0;
    int whiteMoves_ = 0;
    long long matchId_ = 0;      // 每 reset 一次 +1
    std::vector<Move> history_;

    // 内部：以当前 history_ 重建棋盘与状态
    void replayFromHistory();
};

}  // namespace gomoku
