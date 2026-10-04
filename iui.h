// ============================================================
// iui.h - 界面接口（微内核插件化的界面边界）
//
// controller 与 HumanPlayer 只依赖本接口，不依赖任何具体界面实现：
//     TextUi（现 UI 类，文本终端）
//     NullUi（无头：selfplay / 自动测试）
//     未来的图形 / 网页界面只需实现 IUi 并在入口替换实例即可。
//
// 说明：接口方法沿用原 UI 类的签名，保证替换零成本；
//       messageBox/askYesNo 沿用宽字符，兼容原 Windows 文本实现。
// ============================================================
#pragma once
#include "core.h"

class IUi {
public:
    virtual ~IUi() = default;

    virtual void initWindow(int w, int h) = 0;         // 初始化界面
    virtual void close() = 0;                          // 释放界面资源

    virtual void pollMouse() = 0;                      // 拉取一次输入
    virtual int  pollKey() = 0;                        // 读取按键（回放用）
    virtual Pos  hoverPos() const = 0;                 // 当前悬停格（无效表示无）
    virtual Pos  clickPos() const = 0;                 // 最近一次点击格
    virtual bool hasClick() const = 0;                 // 是否有待消费的点击
    virtual void clearClick() = 0;                     // 消费点击

    virtual void render(const Board& board, Pos hover,
                        Pos lastBlack = { -1, -1 }, Pos lastWhite = { -1, -1 },
                        ChessType turn = ChessType::None) = 0;

    virtual void messageBox(const wchar_t* text) = 0;  // 弹/打印消息
    virtual int  askYesNo(const wchar_t* text) = 0;    // 询问是否，返回 1=是
    virtual void setLayout(int gridSize, int xOffset, int yOffset, int boardSize) = 0;
};

// ---- 无头界面实现：所有交互为空操作，供 selfplay / 自动测试使用 ----
class NullUi : public IUi {
public:
    void initWindow(int, int) override {}
    void close() override {}
    void pollMouse() override {}
    int  pollKey() override { return 0; }
    Pos  hoverPos() const override { return { -1, -1 }; }
    Pos  clickPos() const override { return { -1, -1 }; }
    bool hasClick() const override { return false; }
    void clearClick() override {}
    void render(const Board&, Pos, Pos, Pos, ChessType) override {}
    void messageBox(const wchar_t*) override {}
    int  askYesNo(const wchar_t*) override { return 0; }
    void setLayout(int, int, int, int) override {}
};