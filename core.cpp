// ============================================================
// core.cpp - 核心类实现（Board / Judge / Stats）
// ============================================================
#include "core.h"
#include <cstdio>
#include <cstdlib>

// ---------- Board ----------
Board::Board() {
    size_ = ROWS;
    map_ = new int*[size_];
    for (int i = 0; i < size_; i++) {
        map_[i] = new int[size_];
        for (int j = 0; j < size_; j++) map_[i][j] = 0;
    }
}

Board::~Board() {
    if (!map_) return;
    for (int i = 0; i < size_; i++) delete[] map_[i];
    delete[] map_;
}

// 按当前全局 ROWS/COLS 重建棋盘（配置更改尺寸后调用）
void Board::resize() {
    if (map_) {
        for (int i = 0; i < size_; i++) delete[] map_[i];
        delete[] map_;
        map_ = nullptr;
    }
    size_ = ROWS;
    map_ = new int*[size_];
    for (int i = 0; i < size_; i++) {
        map_[i] = new int[size_];
        for (int j = 0; j < size_; j++) map_[i][j] = 0;
    }
}

void Board::clear() {
    for (int i = 0; i < size_; i++)
        for (int j = 0; j < size_; j++) map_[i][j] = 0;
}

bool Board::place(int r, int c, ChessType color) {
    if (!inBounds(r, c)) return false;
    if (map_[r][c] != static_cast<int>(ChessType::None)) return false;
    map_[r][c] = static_cast<int>(color);
    return true;
}

void Board::set(int r, int c, ChessType color) {
    if (!inBounds(r, c)) return;
    map_[r][c] = static_cast<int>(color);
}

ChessType Board::at(int r, int c) const {
    if (!inBounds(r, c)) return ChessType::None;
    return static_cast<ChessType>(map_[r][c]);
}

bool Board::isFull() const {
    for (int i = 0; i < size_; i++)
        for (int j = 0; j < size_; j++)
            if (map_[i][j] == static_cast<int>(ChessType::None)) return false;
    return true;
}

bool Board::inBounds(int r, int c) const {
    return r >= 0 && r < size_ && c >= 0 && c < size_;
}

// ---------- Judge ----------
bool Judge::checkLine(const Board& b, int r, int c, int dr, int dc, int chess, int length) const {
    if (!b.inBounds(r, c)) return false;
    for (int i = 0; i < length; i++) {
        int nr = r + i * dr;
        int nc = c + i * dc;
        if (!b.inBounds(nr, nc)) return false;
        if (b.map_[nr][nc] != chess) return false;
    }
    return true;
}

// 检查最后落子 last 是否使 color 方形成 length 连珠。
// 对每个方向，从 last 向反方向回退 offset(0..length-1) 作为线段起点，
// 再沿正方向检查 length 个连续同色——覆盖 last 处于连珠任意位置的情况。
bool Judge::checkWin(const Board& b, Pos last, ChessType color, int length) const {
    if (!last.valid()) return false;
    int r = last.r, c = last.c;
    int dr[] = { 0, 1, 1, 1 };   // 四个方向：右、下、右下、左下
    int dc[] = { 1, 0, 1, -1 };
    int chess = static_cast<int>(color);
    for (int d = 0; d < 4; d++) {
        for (int offset = 0; offset < length; offset++) {
            int sr = r - offset * dr[d];   // 线段起点 = last 反方向回退 offset
            int sc = c - offset * dc[d];
            if (checkLine(b, sr, sc, dr[d], dc[d], chess, length)) return true;
        }
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

void Stats::printProgress() const {
    system("cls");
    printf("Current black move count: %d\n", black_);
}
