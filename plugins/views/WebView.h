// ============================================================
// plugins/views/WebView.h - 浏览器界面插件
//
// 【它做三件事，且只做三件事】
//   1. 把内核快照 render() 成一个 JSON 帧，缓存起来；
//   2. poll() 时把 HTTP 线程排队的命令转发给 IControllable；
//   3. 用 SSE 把帧推给浏览器。
// 它不渲染任何像素 —— 像素在 webapp/ 的前端里。因此"换个前端皮肤"
// 不需要重新编译 C++，"换个后端"也不需要改前端协议。
//
// 命令是「队列 + 主循环执行」而不是「HTTP 线程直接调用」：
// 这样所有对内核的操作都发生在同一根线上，天然没有并发改棋局的问题。
// ============================================================
#pragma once
#include "../../contracts/IHost.h"
#include "../../contracts/IView.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gomoku {

class HttpServer;

class WebView final : public IView {
public:
    // webRoot：静态资源目录（webapp/）；port：0 = 自动分配
    WebView(std::string webRoot, int port, bool openBrowser, bool headless);
    ~WebView() override;

    std::string id() const override { return "web"; }
    std::string displayName() const override { return "Web (browser)"; }

    void render(const ViewState& state) override;
    bool poll() override;
    void notify(const std::string& message) override;

    // 实际监听端口（start 之后有效）与访问地址
    int port() const { return port_; }
    std::string url() const;

    bool start();   // 绑定端口并开始监听；失败返回 false

    // 自检/排障开关：打开后把命令分发轨迹打到 stderr（默认关闭，正常运行时零开销）
    void setDebugTrace(bool on) { debugTrace_ = on; }

private:
    // ---- HTTP 处理（在 httplib 的工作线程上执行） ----
    HttpReply handleRequest(const std::string& method, const std::string& path,
                            const std::string& body);
    void dispatchCommand(const std::string& json);   // 控制线程：JSON 命令 → 内核调用
    void pushCommand(const std::string& json);       // HTTP 线程：入队

    std::string webRoot_;
    int port_ = 0;
    bool openBrowser_ = false;
    bool headless_ = false;

    std::unique_ptr<HttpServer> server_;

    // 最新一帧（render 写、HTTP 线程读）
    mutable std::mutex frameMtx_;
    std::string lastFrameJson_;
    bool haveFrame_ = false;
    long long frameVersion_ = -1;
    std::condition_variable frameCv_;

    // 待执行命令（HTTP 线程写、poll 执行）
    mutable std::mutex cmdMtx_;
    std::condition_variable cmdCv_;
    std::deque<std::string> commands_;

    // 当前 SSE 订阅者数量。
    //
    // 【为什么必须限流】
    //   cpp-httplib 用固定大小的线程池处理请求，而一条 SSE 连接会**独占一个
    //   工作线程直到它断开**。浏览器刷新、EventSource 自动重连、多开标签页，
    //   都会留下一条还没被回收的旧连接。一旦这些长连接把线程池占满，
    //   后续的 POST /api/cmd 就会排队等不到线程 —— 表现为"网页能看不能下"，
    //   而且越是反复刷新越卡。
    //
    //   单机自用场景下，超过 kMaxSubscribers 条并发订阅已属异常，
    //   直接拒绝并让前端重连，比拖垮整个服务好。
    static constexpr int kMaxSubscribers = 8;
    std::atomic<int> subscriberCount_{ 0 };

    std::atomic<bool> done_{ false };
    bool debugTrace_ = false;
};

// 订阅者计数的 RAII 守卫：连接建立时 +1，断开时 -1。
// 【为什么要 RAII】推送循环有好几个 break 点（done_、写失败…），
// 手动在每处 -1 迟早会漏一处，于是计数只增不减，限流形同虚设。
class SubscriberGuard {
public:
    explicit SubscriberGuard(std::atomic<int>& counter) : counter_(counter) {
        counter_.fetch_add(1);
    }
    ~SubscriberGuard() { counter_.fetch_sub(1); }
    SubscriberGuard(const SubscriberGuard&) = delete;
    SubscriberGuard& operator=(const SubscriberGuard&) = delete;

private:
    std::atomic<int>& counter_;
};

}  // namespace gomoku
