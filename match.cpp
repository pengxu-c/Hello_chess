// ============================================================
// match.cpp - 规则内核实现
// 纯逻辑：落子 → 判胜/判和 → 换手；无任何 IO / 玩家 / 界面依赖。
// ============================================================
#include "match.h"

Match::Match(Board& board, Judge& judge) : board_(board), judge_(judge) {}

void Match::reset() {
    turn_ = ChessType::Black;
    over_ = false;
    winner_ = ChessType::None;
    moveCount_ = 0;
    history_.clear();
}

bool Match::applyMove(int r, int c) {
    if (over_) return false;
    if (!board_.inBounds(r, c) || board_.at(r, c) != ChessType::None) return false;

    board_.place(r, c, turn_);
    history_.push_back({ r, c });
    ++moveCount_;

    if (judge_.checkWin(board_, { r, c }, turn_)) {
        over_ = true;
        winner_ = turn_;
    } else if (board_.isFull()) {
        over_ = true;
        winner_ = ChessType::None;                // 和棋
    } else {
        turn_ = opponent(turn_);
    }
    return true;
}

void Match::undoLastMove(int r, int c) {
    board_.unset(r, c);
    if (!history_.empty()) history_.pop_back();
    if (moveCount_ > 0) --moveCount_;
    over_ = false;
    winner_ = ChessType::None;
    turn_ = (moveCount_ % 2 == 0) ? ChessType::Black : ChessType::White;
}

void Match::syncMoveCount(int count) {
    moveCount_ = count < 0 ? 0 : count;
    over_ = false;
    winner_ = ChessType::None;
    turn_ = (moveCount_ % 2 == 0) ? ChessType::Black : ChessType::White;
    history_.clear();                       // 外部重建棋盘后，历史记录不再可信
}