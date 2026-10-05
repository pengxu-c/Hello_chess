// ============================================================
// app/ViewDriver.h - 界面插件的主循环驱动
//
// 【为什么需要这个类】
//   IView::poll() 是界面插件的输入侧入口，但总得有人去调它。
//   之前这件事散落在各个宿主里（WebHost 自己写了一个 while 循环），
//   结果是"想在别的场景复用 WebView"就必须抄一遍循环 —— 这正是
//   微内核最忌讳的东西。
//
//   现在把这条循环收敛成一个可复用的驱动：
//     * 宿主（CliHost / WebHost）用它驱动前台界面；
//     * 自检、自动化测试用它驱动被测界面；
//     * 想跑 headless 的 WebView？同样一行。
//
//   驱动只依赖 IView 契约，因此任何界面插件都能被它驱动。
// ============================================================
#pragma once
#include "../contracts/IView.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace gomoku {

class ViewDriver {
public:
    // session 可选：用于判断"内核是否已经要求退出"。传 nullptr 表示由 stop() 控制。
    explicit ViewDriver(IView& view, IControllable* session = nullptr)
        : view_(view), session_(session) {}

    ~ViewDriver() { stop(); }

    ViewDriver(const ViewDriver&) = delete;
    ViewDriver& operator=(const ViewDriver&) = delete;

    // 在后台线程持续调用 view.poll()；返回 false 视为界面要求退出。
    void start() {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { loop(); });
    }

    bool running() const { return running_.load(); }

    void stop() {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
    }

private:
    void loop() {
        while (running_.load()) {
            // 界面自己决定是否需要退出（例如用户点了 Quit、或终端输入流结束）
            if (!view_.poll()) break;
            // 内核已经要求退出时，驱动也随之停止，避免空转
            if (session_ && session_->quitting()) break;
        }
        running_.store(false);
    }

    IView& view_;
    IControllable* session_ = nullptr;
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

}  // namespace gomoku
