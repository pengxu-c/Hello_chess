// ============================================================
// ui.cpp - UI 类实现（EasyX 图形库封装）
// 所有原全局布局/尺寸变量的引用已替换为
// 成员 gridSize_/xOffset_/yOffset_/boardSize_ 与 board.size()。
// ============================================================
#include "ui.h"
#include <cmath>
#include <windows.h>

UI::UI() {}
UI::~UI() {}

// 创建指定宽高的 EasyX 图形窗口（保留控制台）。
void UI::initWindow(int w, int h) {
    initgraph(w, h, EX_SHOWCONSOLE);

}

// 关闭图形窗口，释放 EasyX 资源。
void UI::close() {
    closegraph();
}

// 设置网格布局参数（由 GameController::applyBoardLayout 计算后调用）
void UI::setLayout(int gridSize, int xOffset, int yOffset, int boardSize) {
    gridSize_  = gridSize;
    xOffset_   = xOffset;
    yOffset_   = yOffset;
    boardSize_ = boardSize;
}

// 像素坐标转棋盘坐标（O(1) 数学定位，替代逐格遍历）
// 边界检查用成员 boardSize_（由 setLayout 设置，替代原全局棋盘尺寸变量）
Pos UI::pixelToCell(int x, int y) const {
    int c = (x - xOffset_ + gridSize_ / 2) / gridSize_;   // 四舍五入到最近格
    int r = (y - yOffset_ + gridSize_ / 2) / gridSize_;
    if (r >= 0 && r < boardSize_ && c >= 0 && c < boardSize_) return { r, c };
    return { -1, -1 };
}

// 设置 EasyX 填充/线条颜色为指定棋子颜色（消除 drawPieces / drawTurnIndicator 中的重复颜色设置）
// 白棋：白填充 + 黑描边；黑棋：黑填充 + 白描边（棕色背景上清晰可见）
void UI::setPieceColor(ChessType color) {
    if (color == ChessType::White) {
        setfillcolor(WHITE);
        setlinecolor(BLACK);
    } else {
        setfillcolor(BLACK);
        setlinecolor(WHITE);
    }
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

// 当前悬停/点击的棋盘坐标及点击状态查询与消费。
Pos UI::hoverPos() const { return { hoverR_, hoverC_ }; }
Pos UI::clickPos() const { return { clickR_, clickC_ }; }
bool UI::hasClick() const { return hasClick_; }
void UI::clearClick() { hasClick_ = false; }

// 绘制棋盘网格线（行列数取自 board.size()）
void UI::drawGrid(const Board& board) {
    int n = board.size();
    setlinecolor(RGB(139, 69, 19));
    for (int i = 0; i < n; i++)
        line(xOffset_, yOffset_ + i * gridSize_,
             xOffset_ + (n - 1) * gridSize_, yOffset_ + i * gridSize_);
    for (int j = 0; j < n; j++)
        line(xOffset_ + j * gridSize_, yOffset_,
             xOffset_ + j * gridSize_, yOffset_ + (n - 1) * gridSize_);
}

// 绘制所有棋子
void UI::drawPieces(const Board& board) {
    int n = board.size();
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            ChessType t = board.at(i, j);
            if (t == ChessType::None) continue;
            int x = j * gridSize_ + xOffset_;
            int y = i * gridSize_ + yOffset_;
            setPieceColor(t);
            solidcircle(x, y, gridSize_ / 2 - 3);
        }
    }
}

// 绘制鼠标悬停提示圈
void UI::drawHover(Pos hover) {
    if (hover.valid()) {
        setlinecolor(BLUE);
        int x = hover.c * gridSize_ + xOffset_;
        int y = hover.r * gridSize_ + yOffset_;
        circle(x, y, gridSize_ / 2 - 1);
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
        int x = p.c * gridSize_ + xOffset_;
        int y = p.r * gridSize_ + yOffset_;
        setlinecolor(m.ring);
        setlinestyle(PS_SOLID, 3);                                // 加粗圆环更显眼
        circle(x, y, gridSize_ / 2 + 2);                          // 外圈包住棋子
        setlinestyle(PS_SOLID, 1);                                // 恢复默认线宽
    }
}

// 绘制回合指示棋子：位于棋盘最左侧（棋盘外），棋子颜色 = 当前轮到的一方
// None 表示不显示（如回放/恢复场景）
void UI::drawTurnIndicator(ChessType turn) {
    if (turn == ChessType::None) return;
    int x = xOffset_ / 2;                       // 最左侧：棋盘左边界中点
    int y = 300;                               // 垂直居中（窗口高 600）
    // 绘制指示棋子（含白色描边，棕色背景上清晰可见）
    setPieceColor(turn);
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
    drawGrid(board);
    drawPieces(board);
    drawLastMoves(board, lastBlack, lastWhite);
    drawTurnIndicator(turn);
    drawHover(hover);
    EndBatchDraw();
}

// 弹出消息框（仅确认按钮）。
void UI::messageBox(const wchar_t* text) {
    MessageBox(GetHWnd(), text, L"Game Over", MB_OK);
}

// 弹出是/否对话框，返回 IDYES/IDNO。
int UI::askYesNo(const wchar_t* text) {
    return MessageBox(GetHWnd(), text, L"Game Over", MB_YESNO | MB_ICONQUESTION);
}
