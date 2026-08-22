// ============================================================
// ui.h - 用户界面类声明
// 封装所有 EasyX 图形库调用，便于未来替换为其他图形库
// 布局参数（网格像素/偏移/尺寸）已移入成员，由 setLayout() 设置，
// 消除对全局可变布局变量的依赖。
// ============================================================
#pragma once
#include "core.h"
#include <graphics.h>

class UI {
public:
    UI();
    ~UI();
    void initWindow(int w, int h);              // 创建图形窗口
    void close();                               // 关闭窗口
    void pollMouse();                           // 轮询鼠标消息，更新悬停/点击状态
    Pos hoverPos() const;                       // 当前悬停的棋盘坐标
    Pos clickPos() const;                       // 最近一次点击的棋盘坐标
    bool hasClick() const;                      // 本帧是否有新点击
    void clearClick();                          // 消费点击标记
    void render(const Board& board, Pos hover,                      // 渲染棋盘+棋子+提示圈+最后一手标记+回合指示
                Pos lastBlack = { -1, -1 }, Pos lastWhite = { -1, -1 },
                ChessType turn = ChessType::None);              // turn：当前回合方，None=不显示
    void messageBox(const wchar_t* text);       // 弹出提示框
    int askYesNo(const wchar_t* text);          // 弹出是/否对话框，返回选择
    // 设置网格布局参数（替代原全局网格像素/偏移变量）；
    // boardSize 用于 pixelToCell 的边界检查（替代原全局棋盘尺寸变量）
    void setLayout(int gridSize, int xOffset, int yOffset, int boardSize);
private:
    int gridSize_   = 38;    // 每格像素大小（原全局网格像素变量）
    int xOffset_    = 213;   // 棋盘左上角 X 偏移（原全局 X 偏移变量）
    int yOffset_    = 34;    // 棋盘左上角 Y 偏移（原全局 Y 偏移变量）
    int boardSize_  = 15;    // 棋盘尺寸（原全局棋盘尺寸变量，用于 pixelToCell 边界检查）
    ExMessage msg_{};        // EasyX 消息
    int hoverR_ = -1;        // 悬停行
    int hoverC_ = -1;        // 悬停列
    int clickR_ = -1;        // 点击行
    int clickC_ = -1;        // 点击列
    bool hasClick_ = false;  // 本帧是否有点击
    Pos pixelToCell(int x, int y) const;        // 像素坐标转棋盘坐标
    void setPieceColor(ChessType color);        // 设置 EasyX 填充/线条颜色为指定棋子颜色（白棋白填黑线，黑棋黑填白线）
    void drawGrid(const Board& board);          // 绘制网格线（需 board.size() 确定行列数）
    void drawPieces(const Board& board);        // 绘制棋子
    void drawLastMoves(const Board& board, Pos lastBlack, Pos lastWhite);  // 绘制最后一手标记环
    void drawTurnIndicator(ChessType turn);     // 左侧绘制当前回合指示棋子
    void drawHover(Pos hover);                  // 绘制悬停提示圈
};
