// ============================================================
// ui.cpp - UI 类实现（EasyX 图形库封装）
// ============================================================
#include "ui.h"
#include <cstdio>
#include <io.h>
#include <cmath>

UI::UI() {}
UI::~UI() {}

void UI::initWindow(int w, int h) {
    initgraph(w, h, EX_SHOWCONSOLE);
}

void UI::close() {
    closegraph();
}

// 像素坐标转棋盘坐标（找到鼠标所在的最近格子）
Pos UI::pixelToCell(int x, int y) const {
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLS; j++) {
            int cx = j * GRID_SIZE + XOFFSET;
            int cy = i * GRID_SIZE + YOFFSET;
            if (abs(x - cx) < GRID_SIZE / 2 && abs(y - cy) < GRID_SIZE / 2) {
                return { i, j };
            }
        }
    }
    return { -1, -1 };
}

// 轮询鼠标消息：移动更新悬停，左键点击记录落子位置
void UI::pollMouse() {
    hasClick_ = false;
    if (peekmessage(&msg_, EX_MOUSE)) {
        if (msg_.message == WM_MOUSEMOVE) {
            Pos p = pixelToCell(msg_.x, msg_.y);
            hoverR_ = p.r;
            hoverC_ = p.c;
        } else if (msg_.message == WM_LBUTTONDOWN) {
            Pos p = pixelToCell(msg_.x, msg_.y);
            if (p.valid()) {
                clickR_ = p.r;
                clickC_ = p.c;
                hasClick_ = true;
            }
        }
    }
}

Pos UI::hoverPos() const { return { hoverR_, hoverC_ }; }
Pos UI::clickPos() const { return { clickR_, clickC_ }; }
bool UI::hasClick() const { return hasClick_; }
void UI::clearClick() { hasClick_ = false; }

// 绘制棋盘网格线
void UI::drawGrid() {
    setlinecolor(RGB(139, 69, 19));
    for (int i = 0; i < ROWS; i++)
        line(XOFFSET, YOFFSET + i * GRID_SIZE,
             XOFFSET + (COLS - 1) * GRID_SIZE, YOFFSET + i * GRID_SIZE);
    for (int j = 0; j < COLS; j++)
        line(XOFFSET + j * GRID_SIZE, YOFFSET,
             XOFFSET + j * GRID_SIZE, YOFFSET + (ROWS - 1) * GRID_SIZE);
}

// 绘制所有棋子
void UI::drawPieces(const Board& board) {
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLS; j++) {
            ChessType t = board.at(i, j);
            if (t == ChessType::None) continue;
            int x = j * GRID_SIZE + XOFFSET;
            int y = i * GRID_SIZE + YOFFSET;
            if (t == ChessType::White) {
                setfillcolor(WHITE);
                setlinecolor(BLACK);
            } else {
                setfillcolor(BLACK);
                setlinecolor(WHITE);
            }
            solidcircle(x, y, GRID_SIZE / 2 - 3);
        }
    }
}

// 绘制鼠标悬停提示圈
void UI::drawHover(Pos hover) {
    if (hover.valid()) {
        setlinecolor(BLUE);
        int x = hover.c * GRID_SIZE + XOFFSET;
        int y = hover.r * GRID_SIZE + YOFFSET;
        circle(x, y, GRID_SIZE / 2 - 1);
    }
}

// 渲染一帧：背景 → 网格 → 棋子 → 提示圈
void UI::render(const Board& board, Pos hover) {
    BeginBatchDraw();
    if (hasBg_) {
        putimage(0, 0, &imgBg_);
    } else {
        setbkcolor(RGB(240, 220, 180));
        cleardevice();
    }
    drawGrid();
    drawPieces(board);
    drawHover(hover);
    EndBatchDraw();
}

void UI::messageBox(const wchar_t* text) {
    MessageBox(GetHWnd(), text, L"Game Over", MB_OK);
}

int UI::askYesNo(const wchar_t* text) {
    return MessageBox(GetHWnd(), text, L"Game Over", MB_YESNO | MB_ICONQUESTION);
}

void UI::loadBackground(const char* path) {
    if (_access(path, 0) == 0) {
        loadimage(&imgBg_, L"Resource/images/bk.jpg");
        hasBg_ = true;
    }
}

bool UI::hasBackground() const { return hasBg_; }
