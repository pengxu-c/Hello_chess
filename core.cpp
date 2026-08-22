// ============================================================
// core.cpp - 核心类实现（Board / Judge / Stats）
// ============================================================
#include "core.h"
#include <cstdio>
#include <cstdlib>

// ---------- Board ----------
// 默认构造：初始化为 15×15 棋盘（与原默认棋盘尺寸 15 行为一致）
Board::Board() {
    resize(15);
}

// 析构：vector 自动释放，无需手动 delete
Board::~Board() {}

// 按尺寸 n 重建 n×n 棋盘并清零（配置更改尺寸后调用）
// 同时重置空位计数为 n*n（满盘空位）
void Board::resize(int n) {
    size_ = n;
    map_.assign(static_cast<size_t>(n) * n, 0);   // 一维 n*n 全 0
    emptyCount_ = n * n;
}

// 清空棋盘：所有格子归零，空位计数恢复为 size_*size_
// 不改变尺寸 size_ 与连珠数 winLen_
void Board::clear() {
    std::fill(map_.begin(), map_.end(), 0);
    emptyCount_ = size_ * size_;
}

// 落子：仅空位成功，成功时维护 emptyCount_--
bool Board::place(int r, int c, ChessType color) {
    if (!inBounds(r, c)) return false;
    int idx = r * size_ + c;
    if (map_[idx] != static_cast<int>(ChessType::None)) return false;
    map_[idx] = static_cast<int>(color);
    emptyCount_--;
    return true;
}

// 直接设置（模拟用，不校验空位）
// 注意：此函数不维护 emptyCount_，仅用于 AI 模拟/回放等"已知目标状态"场景，
//       调用方需保证最终状态与 emptyCount_ 一致（如回放前先 clear()）。
void Board::set(int r, int c, ChessType color) {
    if (!inBounds(r, c)) return;
    map_[static_cast<size_t>(r) * size_ + c] = static_cast<int>(color);
}

// 读取某位置（越界返回 ChessType::None）
ChessType Board::at(int r, int c) const {
    if (!inBounds(r, c)) return ChessType::None;
    return static_cast<ChessType>(map_[static_cast<size_t>(r) * size_ + c]);
}

// 棋盘是否已满（O(1)，基于空位计数）
bool Board::isFull() const {
    return emptyCount_ == 0;
}

// 坐标是否在棋盘内
bool Board::inBounds(int r, int c) const {
    return r >= 0 && r < size_ && c >= 0 && c < size_;
}

// ---------- Judge ----------
// 检查最后落子 last 是否使 color 方形成连珠（连数取自 b.winLen()）。
// 复用 scanLine 统一线段统计核：对 4 方向统计经过 last 的连续同色长度，
// 任一方向 count >= winLen 即成连。消除原 checkLine（offset 回退法）的独立实现，
// 与 player.cpp 的 inlineCheckN 共用同一套连珠判定逻辑。
bool Judge::checkWin(const Board& b, Pos last, ChessType color) const {
    if (!last.valid()) return false;
    int winLen = b.winLen();
    int dr[] = { 0, 1, 1, 1 };   // 四个方向：右、下、右下、左下
    int dc[] = { 1, 0, 1, -1 };
    for (int d = 0; d < 4; d++) {
        if (scanLine(b, last.r, last.c, dr[d], dc[d], color).count >= winLen) return true;
    }
    return false;
}

// ---------- Stats ----------
void Stats::reset() {
    black_ = 0;
    white_ = 0;
}

void Stats::recordMove(ChessType color) {
    if (color == ChessType::Black) black_++;
    else if (color == ChessType::White) white_++;
}

int Stats::count(ChessType color) const {
    if (color == ChessType::Black) return black_;
    if (color == ChessType::White) return white_;
    return 0;
}
