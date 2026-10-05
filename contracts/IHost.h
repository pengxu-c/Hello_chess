// ============================================================
// contracts/IHost.h - 「HTTP 请求 → 一个回答」的宿主契约
//
// 这一层刻意不认识棋局：它只知道「有人请求了 /api/xxx，回答这段内容」。
// 于是：
//   * 换 HTTP 库（httplib → Boost.Beast → 自研）只改实现；
//   * 换传输协议（WebSocket / gRPC / 命名管道）只需另写一个 Host 实现；
//   * 加新接口不用碰 HTTP 层，更不用碰内核。
//
// 放在 contracts 而不是 hosts 里，是因为界面插件（WebView）需要实现
// webRoot/port 之类的宿主能力，而 hosts 是它的下游 —— 依赖只能向下。
// ============================================================
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace gomoku {

// 一次 HTTP 回答
struct HttpReply {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

// 持续推送（SSE）的连接句柄。
// push() 写出一段数据（返回 false 表示客户端已断开，实现应立即结束推送）；
// poll() 最多等待 timeoutMs，返回 false 表示连接或宿主应关闭。
class IStream {
public:
    virtual ~IStream() = default;
    virtual bool push(const std::string& data) = 0;
    virtual bool poll(int timeoutMs) = 0;
};

using RequestHandler = std::function<HttpReply(const std::string& method,
                                              const std::string& path,
                                              const std::string& body)>;
using StreamHandler = std::function<void(IStream& stream)>;

// 宿主：由具体 HTTP 实现满足。
class Host {
public:
    virtual ~Host() = default;
    virtual void onRequest(RequestHandler handler) = 0;
    // 注册需要持续推送的路径（SSE）。实现应把这些路径单独路由。
    virtual void onStream(const std::vector<std::string>& paths,
                          StreamHandler handler) = 0;
};

}  // namespace gomoku
