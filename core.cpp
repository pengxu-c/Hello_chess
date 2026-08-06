// ============================================================
// core.cpp - 核心类实现（Board / Judge / Stats）
// ============================================================
#include "core.h"
#include <cstdio>
#include <cstdlib>

// ---------- Board ----------
Board::Board() {
    map_ = new int*[ROWS];
    for (int i = 0; i < ROWS; i++) {
        map_[i] = new int[COLS];
        for (int j = 0; j < COLS; j++) map_[i][j] = 0;
    }
}

Board::~Board() {
    if (!map_) return;
    for (int i = 0; i < ROWS; i++) delete[] map_[i];
    delete[] map_;
}

void Board::clear() {
    for (int i = 0; i < ROWS; i++)
        for (int j = 0; j < COLS; j++) map_[i][j] = 0;
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
    for (int i = 0; i < ROWS; i++)
        for (int j = 0; j < COLS; j++)
            if (map_[i][j] == static_cast<int>(ChessType::None)) return false;
    return true;
}

bool Board::inBounds(int r, int c) const {
    return r >= 0 && r < ROWS && c >= 0 && c < COLS;
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

bool Judge::checkWin(const Board& b, Pos last, ChessType color, int length) const {
    if (!last.valid()) return false;
    int r = last.r, c = last.c;
    int dr[] = { 0, 1, 1, 1 };   // 四个方向：右、下、右下、左下
    int dc[] = { 1, 0, 1, -1 };
    int chess = static_cast<int>(color);
    for (int d = 0; d < 4; d++) {
        for (int offset = 0; offset < length; offset++) {
            int sr = r - offset * dr[d];
            int sc = c - offset * dc[d];
            if (checkLine(b, sr, sc, dr[d], dc[d], chess, length)) return true;
        }
    }
    return false;
}

bool Judge::checkFive(const Board& b, Pos last, ChessType color) const {
    return checkWin(b, last, color, 5);
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
