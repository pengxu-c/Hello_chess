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
        svr_->Get(path, [this](const httplib::Request&, httplib::Response& res) {
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
        std::fprintf(stderr,
                     "[http] bind %s:%d failed, errno=%d (%s)\n",
                     "127.0.0.1", port_, errno, std::strerror(errno));
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
}

}  // namespace gomoku
