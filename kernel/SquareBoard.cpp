// ============================================================
// kernel/SquareBoard.cpp - 默认棋盘实现
// ============================================================
#include "SquareBoard.h"
#include <algorithm>

namespace gomoku {

void SquareBoard::reset(int n) {
    if (n < 1) n = 1;
    size_ = n;
    cells_.assign(static_cast<size_t>(n) * n, static_cast<int>(Stone::Empty));
    empty_ = n * n;
}

void SquareBoard::clear() {
    std::fill(cells_.begin(), cells_.end(), static_cast<int>(Stone::Empty));
    empty_ = size_ * size_;
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
