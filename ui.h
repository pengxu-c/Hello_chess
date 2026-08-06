// ============================================================
// ui.h - 用户界面类声明
// 封装所有 EasyX 图形库调用，便于未来替换为其他图形库
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
    void render(const Board& board, Pos hover); // 渲染棋盘+棋子+提示圈
    void messageBox(const wchar_t* text);       // 弹出提示框
    int askYesNo(const wchar_t* text);          // 弹出是/否对话框，返回选择
    void loadBackground(const char* path);      // 加载背景图
    bool hasBackground() const;                 // 是否已加载背景
private:
    ExMessage msg_{};        // EasyX 消息
    int hoverR_ = -1;        // 悬停行
    int hoverC_ = -1;        // 悬停列
    int clickR_ = -1;        // 点击行
    int clickC_ = -1;        // 点击列
    bool hasClick_ = false;  // 本帧是否有点击
    bool hasBg_ = false;     // 是否有背景图
    IMAGE imgBg_{};          // 背景图
    Pos pixelToCell(int x, int y) const;        // 像素坐标转棋盘坐标
    void drawGrid();                            // 绘制网格线
    void drawPieces(const Board& board);        // 绘制棋子
    void drawHover(Pos hover);                  // 绘制悬停提示圈
};
