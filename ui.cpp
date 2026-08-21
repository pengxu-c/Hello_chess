// ============================================================
// ui.cpp - UI 类实现（EasyX 图形库封装）
// ============================================================
#include "ui.h"
#include <cmath>
#include <windows.h>

UI::UI() {}
UI::~UI() {}

void UI::initWindow(int w, int h) {
    initgraph(w, h, EX_SHOWCONSOLE);

}

void UI::close() {
    closegraph();
}

// 像素坐标转棋盘坐标（O(1) 数学定位，替代逐格遍历）
Pos UI::pixelToCell(int x, int y) const {
    int c = (x - XOFFSET + GRID_SIZE / 2) / GRID_SIZE;   // 四舍五入到最近格
    int r = (y - YOFFSET + GRID_SIZE / 2) / GRID_SIZE;
    if (r >= 0 && r < ROWS && c >= 0 && c < COLS) return { r, c };
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

// 绘制最后一手闪烁标记：黑方红环、白方蓝环，随时间交替亮暗
// 依赖传入的 lastMove 坐标；并校验该格棋子颜色与回合语义一致，防止误标
// 绘制最后一手标记环：黑方黑环 / 白方白环（静态不闪烁）
void UI::drawLastMoves(const Board& board, Pos lastBlack, Pos lastWhite) {
    struct Marker { Pos p; ChessType color; COLORREF ring; };
    Marker marks[] = {
        { lastBlack, ChessType::Black, RGB(0, 0, 0) },          // 黑方：黑色环
        { lastWhite, ChessType::White, RGB(255, 255, 255) },    // 白方：白色环
    };
    for (const auto& m : marks) {
        Pos p = m.p;
        if (!p.valid() || !board.inBounds(p.r, p.c)) continue;   // 无标记或越界则跳过
        if (board.at(p.r, p.c) != m.color) continue;             // 颜色不一致（撤销等）则不画
        int x = p.c * GRID_SIZE + XOFFSET;
        int y = p.r * GRID_SIZE + YOFFSET;
        setlinecolor(m.ring);
        setlinestyle(PS_SOLID, 3);                                // 加粗圆环更显眼
        circle(x, y, GRID_SIZE / 2 + 2);                          // 外圈包住棋子
        setlinestyle(PS_SOLID, 1);                                // 恢复默认线宽
    }
}

// 绘制回合指示棋子：位于棋盘最左侧（棋盘外），棋子颜色 = 当前轮到的一方
// None 表示不显示（如回放/恢复场景）
void UI::drawTurnIndicator(ChessType turn) {
    if (turn == ChessType::None) return;
    int x = XOFFSET / 2;                       // 最左侧：棋盘左边界中点
    int y = 300;                               // 垂直居中（窗口高 600）
    // 绘制指示棋子（含白色描边，棕色背景上清晰可见）
    if (turn == ChessType::White) {
        setfillcolor(WHITE);
        setlinecolor(BLACK);
    } else {
        setfillcolor(BLACK);
        setlinecolor(WHITE);
    }
    solidcircle(x, y, 22);
    // 下方标注当前回合方
    settextcolor(BLACK);
    setbkcolor(RGB(240, 220, 180));
    settextstyle(16, 0, L"Microsoft YaHei");
    outtextxy(x - 40, y + 30, turn == ChessType::Black ? L"[ Black to move ]" : L"[ White to move ]");
}

// 渲染一帧：背景色 → 网格 → 棋子 → 最后一手标记 → 回合指示 → 提示圈
void UI::render(const Board& board, Pos hover, Pos lastBlack, Pos lastWhite, ChessType turn) {
    BeginBatchDraw();
    setbkcolor(RGB(240, 220, 180));
    cleardevice();
    drawGrid();
    drawPieces(board);
    drawLastMoves(board, lastBlack, lastWhite);
    drawTurnIndicator(turn);
    drawHover(hover);
    EndBatchDraw();
}

void UI::messageBox(const wchar_t* text) {
    MessageBox(GetHWnd(), text, L"Game Over", MB_OK);
}

int UI::askYesNo(const wchar_t* text) {
    return MessageBox(GetHWnd(), text, L"Game Over", MB_YESNO | MB_ICONQUESTION);
}
