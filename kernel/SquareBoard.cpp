// ============================================================
// kernel/SquareBoard.cpp - 默认棋盘实现
// ============================================================
#include "SquareBoard.h"
#include <algorithm>

namespace gomoku {

void SquareBoard::reset(int n) {
    if (n < 1) n = 1;
    if (n > RulesConfig::kMaxBoardSize) n = RulesConfig::kMaxBoardSize;  // 经 IBoard.h 间接包含 GameTypes.h
    size_ = n;
    cells_.assign(static_cast<size_t>(n) * static_cast<size_t>(n), static_cast<int>(Stone::Empty));
    empty_ = static_cast<int>(cells_.size());
}

void SquareBoard::clear() {
    std::fill(cells_.begin(), cells_.end(), static_cast<int>(Stone::Empty));
    empty_ = static_cast<int>(cells_.size());
}

bool SquareBoard::place(int r, int c, Stone s) {
    if (!inBounds(r, c) || s == Stone::Empty) return false;
    if (cells_[idx(r, c)] != static_cast<int>(Stone::Empty)) return false;
    cells_[idx(r, c)] = static_cast<int>(s);
    --empty_;
    return true;
}

bool SquareBoard::unset(int r, int c) {
    if (!inBounds(r, c)) return false;
    if (cells_[idx(r, c)] == static_cast<int>(Stone::Empty)) return false;
    cells_[idx(r, c)] = static_cast<int>(Stone::Empty);
    ++empty_;
    return true;
}

}  // namespace gomoku
