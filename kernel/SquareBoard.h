// ============================================================
// kernel/SquareBoard.h - 默认棋盘实现（N×N，一维数组存储）
//
// 维护 emptyCount_：isFull()/isEmpty() 为 O(1)，悔棋回退不会失真。
// 这是内核里唯一「知道棋子怎么存」的地方。
// ============================================================
#pragma once
#include "IBoard.h"
#include <vector>

namespace gomoku {

class SquareBoard final : public IBoard {
public:
    SquareBoard() = default;
    explicit SquareBoard(int n) { reset(n); }

    int size() const override { return size_; }
    void reset(int n) override;
    void clear() override;

    bool inBounds(int r, int c) const override {
        return r >= 0 && c >= 0 && r < size_ && c < size_;
    }
    Stone at(int r, int c) const override {
        if (!inBounds(r, c)) return Stone::Empty;
        return static_cast<Stone>(cells_[idx(r, c)]);
    }

    bool place(int r, int c, Stone s) override;
    bool unset(int r, int c) override;

    bool isFull() const override { return empty_ == 0; }
    bool isEmpty() const override { return empty_ == size_ * size_; }
    int  stoneCount() const override { return size_ * size_ - empty_; }

    std::vector<int> toCells() const override { return cells_; }

private:
    size_t idx(int r, int c) const { return static_cast<size_t>(r) * size_ + c; }

    std::vector<int> cells_;   // r*size_+c，值取 Stone 的 int
    int size_ = 0;
    int empty_ = 0;
};

}  // namespace gomoku
