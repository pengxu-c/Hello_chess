// ============================================================
// contracts/IView.h - 前端界面插件契约（微内核的「界面」边界）
//
// 一个界面 = 实现本接口的一个类 + 在 plugins/ViewRegistry.cpp 注册一行。
// 不需要改内核、不需要改玩家、不需要改宿主。
//
// 界面的职责只有三件（刻意保持极窄）：
//   1. render()  把内核快照画出来 / 转成协议（网页、SSE、终端、图片、日志都行）
//   2. poll()    处理自己的输入，需要操作棋局时调用 IControllable
//   3. notify()  显示一条人类看的提示（可选）
//
// 界面拿不到棋盘对象、拿不到玩家对象 —— 换界面绝不可能改坏棋局。
// ============================================================
#pragma once
#include "GameTypes.h"
#include "IControllable.h"

namespace gomoku {

class IView {
public:
    virtual ~IView() = default;

    // 由内核在注册观察者时回填（界面插件不要自己赋值）
    virtual void attach(IControllable* session) { session_ = session; }
    IControllable* session() const { return session_; }

    virtual std::string id() const = 0;            // 界面插件 id（"cli" / "web"）
    virtual std::string displayName() const = 0;   // 人类可读名

    // ---- 1. 渲染：收到新状态快照 ----
    // 保证在注册后立即收到一次，之后每次状态变化再收到一次。
    // 【线程约束】本方法在控制线程或通知线程上被调用；实现必须自己保证线程安全，
    // 且不得长时间阻塞（耗时输出请自行缓冲/异步）。
    virtual void render(const ViewState& state) = 0;

    // ---- 2. 输入：主循环每帧调用一次 ----
    // 返回 false 表示界面希望退出程序（等价于 session()->requestQuit()）。
    virtual bool poll() { return true; }

    // ---- 3. 提示（可选）：内核产生的消息，如 "Cell occupied" ----
    virtual void notify(const std::string& message) { (void)message; }

protected:
    IControllable* session_ = nullptr;   // 由 attach() 回填，生命周期由宿主保证
};

}  // namespace gomoku
