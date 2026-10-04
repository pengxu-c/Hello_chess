// ============================================================
// ui.h - 文本终端界面实现（IUi 的一个具体实现）
// 原 EasyX 图形实现已移除；本类在控制台以文本方式渲染棋盘并读取输入。
// 接口已抽到 iui.h：controller / player 只依赖 IUi，本类是默认插件。
// ============================================================
#pragma once
#include "iui.h"
#include <string>
#include <vector>

class UI : public IUi {
public:
    UI();
    ~UI() override;

    void initWindow(int w, int h) override;      // 文本模式：打印一次操作提示
    void close() override;                       // 文本模式：无资源需释放
    void pollMouse() override;                   // 非阻塞读取一行落子坐标
    int  pollKey() override;                     // 非阻塞读取按键虚拟码（回放用）
    Pos  hoverPos() const override;              // 文本模式无悬停，恒返回无效坐标
    Pos  clickPos() const override;              // 最近一次输入的落子坐标
    bool hasClick() const override;              // 是否有待消费的落子输入
    void clearClick() override;                  // 消费落子输入标记
    void render(const Board& board, Pos hover,                      // 打印棋盘文本
                Pos lastBlack = { -1, -1 }, Pos lastWhite = { -1, -1 },
                ChessType turn = ChessType::None) override;
    void messageBox(const wchar_t* text) override;       // 打印消息
    int  askYesNo(const wchar_t* text) override;         // 询问 y/n，返回 1=是
    // 文本模式不使用像素布局，仅记录 boardSize 用于输入范围校验
    void setLayout(int gridSize, int xOffset, int yOffset, int boardSize) override;

private:
    int boardSize_ = 15;                 // 棋盘尺寸（输入范围校验用）
    int clickR_ = -1;                    // 待消费的落子行
    int clickC_ = -1;                    // 待消费的落子列
    bool hasClick_ = false;              // 是否有待消费的落子
    std::string toNarrow(const wchar_t* w) const;   // 宽字符转本地编码，供 printf
    void drawBoard(const Board& board, Pos lastBlack, Pos lastWhite, ChessType turn);

    // 渲染快照：棋盘无变化时跳过重绘，避免人类回合每帧清屏导致闪烁
    std::vector<int> snapshot_;
    Pos lastB_{ -1, -1 };
    Pos lastW_{ -1, -1 };
    ChessType lastTurn_ = ChessType::None;
};