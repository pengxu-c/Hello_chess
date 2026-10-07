// ============================================================
// plugins/views/EasyXView.h - EasyX 图形窗口界面插件
//
// 【它是什么】
//   一个"本地窗口"界面插件：开一个 Win32 图形窗口，用 EasyX 画棋盘，
//   鼠标点击落子。不需要浏览器、不需要端口、不需要任何网络栈。
//
// 【与内核的边界】
//   实现 IView 后能做的只有三件：
//     render() 把 ViewState 快照交下去；
//     poll()   轮询鼠标，需要落子时调 session_->play()；
//     notify() 显示一条提示。
//   拿不到棋盘对象、拿不到玩家对象 —— 所以换前端绝不可能改坏棋局。
//
// 【启动方式】
//   hello_chess.exe --view easyx
//   与 cli / web 完全并列，不特殊、不需要额外配置。
//
// 【与旧版的关系】
//   绘制部分复用 engine/legacy_easyx/ui.{h,cpp}
//   （从 git 取回的 2026-09 原件，逻辑一行未改），
//   本类只负责"新内核 ↔ 旧引擎类型"的转换与线程分工。
// ============================================================
#pragma once

// easyx.h 会带入 windows.h，而 Windows 头把 min/max 定义成宏，
// 会把 std::min / std::max 破坏成 ((a)<(b)?(a):(b))。
// NOMINMAX 必须在包含 easyx.h 之前定义，这个头文件先兜住底。
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../../contracts/IView.h"
#include "../../engine/core.h"   // 旧引擎 Board/Pos，供 legacy UI 使用
#include "../../engine/legacy_easyx/ui.h"   // 旧版 EasyX 界面（class UI，全局命名空间）

#include <memory>
#include <string>

namespace gomoku {

class EasyXView final : public IView {
public:
    explicit EasyXView(bool headless);
    ~EasyXView() override;

    std::string id() const override { return "easyx"; }
    std::string displayName() const override { return "EasyX (window)"; }

    void render(const ViewState& state) override;
    bool poll() override;
    void notify(const std::string& message) override;

    // 创建图形窗口。宿主在注册后立刻调用，以便尽早发现创建失败。
    bool open();
    void close();

private:
    void drawStatus();        // 底部状态栏（状态/对手/步数/提示）
    void ensureWindow();      // 真正建窗；必须且仅能在 poll() 所在线程调用

    bool headless_ = false;
    bool open_ = false;
    bool windowReady_ = false;   // 窗口是否已真正创建（initgraph 已执行）
    int boardSize_ = 15;

    // 布局参数（窗口创建时定下，之后不变；绘制时要用）
    int grid_ = 38;
    int xOffset_ = 213;
    int marginY_ = 34;

    // 旧版 UI（持有窗口与 EasyX 消息槽）。
    // 它是全局命名空间的类（class UI），见 ui.h。
    std::unique_ptr<UI> legacy_;

    // 给旧 UI 画的棋盘（每次 render 从 ViewState 重铺一遍）
    Board legacyBoard_;

    // ---- 最新快照（render 写、poll 读）----
    ViewState last_;
    std::string pendingMessage_;   // notify() 写入，poll() 消费
    bool hasMessage_ = false;
};

}  // namespace gomoku