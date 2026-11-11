// ============================================================
// hosts/web/HttpServer.cpp - Host 的 httplib 实现
// ============================================================
#include "HttpServer.h"

#include "httplib.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace gomoku {
namespace {

// ---- 把 httplib 的 DataSink 包成 IStream，交给界面插件去推 ----
class SinkStream final : public IStream {
public:
    SinkStream(httplib::DataSink& sink, const std::atomic<bool>& running)
        : sink_(sink), running_(running) {}

    bool push(const std::string& data) override {
        if (!running_.load()) return false;
        return sink_.write(data.data(), data.size());
    }

    bool poll(int timeoutMs) override {
        if (!running_.load()) return false;
        // httplib 的 sink 本身没有可等待的事件，这里用短睡眠让出 CPU；
        // 上层（WebView）自己用条件变量等新帧，因此这里的等待只影响断开检测。
        (void)timeoutMs;
        return running_.load();
    }

private:
    httplib::DataSink& sink_;
    const std::atomic<bool>& running_;
};

// ---- Host/Origin 前置校验：只接受来自本机的请求 ----
// 服务虽只监听 127.0.0.1，但浏览器仍可能被诱导访问 http://127.0.0.1:端口
// （DNS rebinding / 跨站表单）。Host 与 Origin 都可能带端口，IPv6 还可能是
// "[::1]:8099" 或裸 "::1"，因此先归一化再比对白名单。
std::string stripHostPort(std::string host) {
    // 去首尾空白（部分客户端会在 header 值里留空格）
    const size_t begin = host.find_first_not_of(" \t");
    if (begin == std::string::npos) return std::string();
    const size_t end = host.find_last_not_of(" \t");
    host = host.substr(begin, end - begin + 1);

    // 方括号包裹的 IPv6："[::1]:8099" / "[::1]" → 保留 "[::1]"（丢弃其后的:端口）
    if (!host.empty() && host.front() == '[') {
        const size_t close = host.find(']');
        if (close != std::string::npos) return host.substr(0, close + 1);
    }
    // 普通 "host:port"：恰好一个冒号才当作端口分隔符；
    // 多个冒号是裸 IPv6（如 "::1"），不能误切。
    const size_t first = host.find(':');
    if (first != std::string::npos &&
        host.find(':', first + 1) == std::string::npos) {
        return host.substr(0, first);
    }
    return host;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool isLocalHostName(const std::string& host) {
    const std::string h = toLowerAscii(stripHostPort(host));
    return h == "127.0.0.1" || h == "localhost" ||
           h == "[::1]" || h == "::1";
}

// 从 Origin 里抽出 "host[:port]"：去掉 scheme:// 与可能的路径。
std::string originHost(const std::string& origin) {
    const size_t schemeEnd = origin.find("://");
    std::string rest = (schemeEnd != std::string::npos)
                           ? origin.substr(schemeEnd + 3)
                           : origin;
    const size_t slash = rest.find('/');
    if (slash != std::string::npos) rest = rest.substr(0, slash);
    return rest;
}

bool requestFromLocalOrigin(const httplib::Request& req) {
    // Host 为空放行：兼容不带 Host 的简易客户端 / HTTP/1.0。
    const std::string host = req.get_header_value("Host");
    if (!host.empty() && !isLocalHostName(host)) return false;

    // Origin 为空一律放行（硬约束）：同源 GET/SSE 与进程内 httplib::Client
    // 都不发 Origin，且 JS 无法手动设置该头，不能要求它必须存在。
    const std::string origin = req.get_header_value("Origin");
    if (origin.empty()) return true;
    return isLocalHostName(originHost(origin));
}

}  // namespace

HttpServer::HttpServer(std::string root, int port)
    : root_(std::move(root)), port_(port),
      svr_(std::make_unique<httplib::Server>()) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::onRequest(RequestHandler handler) {
    requestHandler_ = std::move(handler);
}

void HttpServer::onStream(const std::vector<std::string>& paths, StreamHandler handler) {
    streamPaths_ = paths;
    streamHandler_ = std::move(handler);
}

void HttpServer::registerRoutes() {
    // ---- 1. 流式路径（SSE）：每个路径单独注册，用 chunked provider 持续推送 ----
    for (const auto& path : streamPaths_) {
        svr_->Get(path, [this](const httplib::Request& req, httplib::Response& res) {
            if (!requestFromLocalOrigin(req)) {
                res.status = 403;
                res.set_content("{\"ok\":false,\"error\":\"forbidden origin\"}",
                                "application/json; charset=utf-8");
                return;
            }
            res.set_header("Cache-Control", "no-cache");
            res.set_header("X-Accel-Buffering", "no");
            StreamHandler handler = streamHandler_;   // 拷贝：provider 可能晚于本帧执行
            res.set_chunked_content_provider(
                "text/event-stream",
                [this, handler](size_t, httplib::DataSink& sink) -> bool {
                    if (!handler) return false;
                    // IStream 必须绑定到 provider 给的 sink，因此在这里构造
                    SinkStream stream(sink, running_);
                    handler(stream);   // 推送循环：返回即表示本连接结束
                    return false;      // 关闭连接
                },
                [](bool) {});
        });
    }

    // ---- 2. 兜底：所有其它请求交给上层的 requestHandler_ ----
    auto handle = [this](const httplib::Request& req, httplib::Response& res) {
        if (!requestFromLocalOrigin(req)) {
            res.status = 403;
            res.set_content("{\"ok\":false,\"error\":\"forbidden origin\"}",
                            "application/json; charset=utf-8");
            return;
        }
        HttpReply reply;
        if (requestHandler_) {
            reply = requestHandler_(req.method, req.path, req.body);
        } else {
            reply.status = 500;
            reply.body = "{\"ok\":false,\"error\":\"no request handler\"}";
        }
        res.status = reply.status;
        res.set_content(reply.body, reply.contentType);
    };

    svr_->Get(R"(/.*)", handle);
    svr_->Post(R"(/.*)", handle);
    svr_->Put(R"(/.*)", handle);
}

bool HttpServer::start() {
#ifdef _WIN32
    // 【必须显式初始化 Winsock，不能指望 httplib 的 static WSInit】
    //   实测症状：bind 返回失败，WSAGetLastError() = 10106
    //   （WSAEPROVIDERFAILEDINIT，服务提供者初始化失败），也就是 socket()
    //   在一个"从未 WSAStartup 过"的进程里被调用。
    //   显式初始化 + 检查返回值，失败时能立刻说清是哪一步。
    if (!wsaReady_) {
        WSADATA wsaData;
        const int wsaRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsaRet != 0) {
            std::fprintf(stderr, "[http] WSAStartup failed: %d\n", wsaRet);
            std::fflush(stderr);
            return false;
        }
        // WSAStartup 返回 0 也可能是「假成功」（协商到的 wVersion 并非 2.2）。
        // 校验实际版本，不符就回收并失败，避免后续 socket() 报 10106 却无从定位。
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            std::fprintf(stderr,
                         "[http] WSAStartup negotiated unsupported wVersion: %d.%d\n",
                         LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
            std::fflush(stderr);
            WSACleanup();
            return false;
        }
        wsaReady_ = true;
    }

    // 探针：进程内先建一个 socket，把"Winsock 到底能不能用"单独验一遍。
    //   失败时把 WSAStartup 的协商结果一起打出来 —— 因为 WSAStartup 返回 0
    //   也可能是假成功（wVersion 为 0），而 bind 的错误码看不出这一层。
    //   成功时保持安静，不污染正常运行时的控制台输出。
    {
        SOCKET probeSock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (probeSock == INVALID_SOCKET) {
            const int probeErr = WSAGetLastError();
            std::fprintf(stderr, "[http] probe socket() failed, wsa=%d\n", probeErr);
            std::fprintf(stderr, "[http]   hint: 10106 = WSAEPROVIDERFAILEDINIT，"
                                 "不是端口问题；检查安全软件/网络过滤驱动是否拦了本进程\n");
            std::fflush(stderr);
        } else {
            ::closesocket(probeSock);
        }
    }
#endif

    if (!root_.empty()) {
        // 静态资源：URL "/" → root_ 目录。
        // 【必须检查返回值】httplib 的 set_mount_point 在目录不存在时静默返回 false，
        // 之后所有静态请求都会 404 —— 而 bind 依然成功，于是问题被推迟到
        // "浏览器打开是空白页"才暴露，排查成本极高。这里提前失败并说清楚。
        if (!svr_->set_mount_point("/", root_)) {
            std::fprintf(stderr,
                         "[http] web root is not a readable directory: %s\n"
                         "[http] static files would 404; refusing to start.\n",
                         root_.c_str());
            std::fflush(stderr);
            return false;
        }
    }
    registerRoutes();

    const int bound = (port_ > 0) ? (svr_->bind_to_port("127.0.0.1", port_) ? port_ : -1)
                                  : svr_->bind_to_any_port("127.0.0.1");
    if (bound <= 0) {
        // 绑定失败时必须说清楚是"端口被占"还是"权限/套接字耗尽"：
        // 上层（ViewRegistry 工厂）只会把 nullptr 一路传上去，
        // 没有这条诊断的话，界面表现为"web view unavailable"，
        // 而真正的原因（errno）会被彻底丢掉。
        //
        // 【为什么必须打 WSAGetLastError，而不是 errno】
        //   Windows 上 socket/bind 的错误码走 Winsock（WSAxxxx），**不会写进 errno**。
        //   旧代码只打 errno，于是绑定失败时打印的是上一次 CRT 调用（例如
        //   fopen("config.json") 失败）留下的 2 = ENOENT —— 看起来像"文件找不到"，
        //   把排查方向彻底带偏。两边的值都打出来，才能看出哪个是真的。
#ifdef _WIN32
        const int wsa = WSAGetLastError();
        std::fprintf(stderr,
                     "[http] bind 127.0.0.1:%d failed, wsa=%d, errno=%d (%s), "
                     "server_valid=%d\n",
                     port_, wsa, errno, std::strerror(errno),
                     svr_->is_valid() ? 1 : 0);
#else
        std::fprintf(stderr,
                     "[http] bind %s:%d failed, errno=%d (%s)\n",
                     "127.0.0.1", port_, errno, std::strerror(errno));
#endif
        std::fflush(stderr);
        return false;
    }
    port_ = bound;

    running_.store(true);
    listenThread_ = std::thread([this] {
        svr_->listen_after_bind();
        running_.store(false);
        std::lock_guard<std::mutex> lk(mtx_);
        stopped_ = true;
        cv_.notify_all();
    });
    return true;
}

void HttpServer::wait() {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [this] { return stopped_; });
}

void HttpServer::stop() {
    running_.store(false);
    if (svr_) svr_->stop();   // 会关闭所有挂起的 SSE 连接，provider 随即返回

    {
        std::lock_guard<std::mutex> lk(mtx_);
        stopped_ = true;
    }
    cv_.notify_all();

    if (listenThread_.joinable()) listenThread_.join();

#ifdef _WIN32
    // 与 start() 里的 WSAStartup 配对。stop() 可能被调用多次（析构里也会调），
    // wsaReady_ 保证只回收一次。
    if (wsaReady_) {
        WSACleanup();
        wsaReady_ = false;
    }
#endif
}

}  // namespace gomoku
