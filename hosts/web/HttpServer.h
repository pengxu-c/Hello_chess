// ============================================================
// hosts/web/HttpServer.h - 基于 cpp-httplib 的 Host 实现
//
// 这是全项目唯一包含 httplib.h 的宿主代码，也是唯一知道「HTTP 存在」的地方。
// 想换掉 HTTP 库，只需再写一个 Host 实现文件，其它代码一行不改。
//
// 生命周期：start() 绑定端口并在后台线程监听；stop() 通知退出并回收线程。
// 静态资源用 httplib 的 mount point（自带 MIME 推断与目录穿越防护），
// 因此本项目不再自己维护一份 asset_provider。
// ============================================================
#pragma once
#include "../../contracts/IHost.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httplib { class Server; }

namespace gomoku {

class HttpServer final : public Host {
public:
    // root 为静态资源目录；port 为 0 时自动分配空闲端口
    HttpServer(std::string root, int port);
    ~HttpServer() override;

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void onRequest(RequestHandler handler) override;
    void onStream(const std::vector<std::string>& paths, StreamHandler handler) override;

    bool start();          // 绑定端口 + 启动监听线程
    void wait();           // 阻塞直到 stop()
    void stop();           // 通知退出并回收线程
    int  port() const { return port_; }

private:
    void registerRoutes();

    std::string root_;
    int port_ = 0;
    std::unique_ptr<httplib::Server> svr_;
    std::thread listenThread_;

    RequestHandler requestHandler_;
    std::vector<std::string> streamPaths_;
    StreamHandler streamHandler_;
    std::atomic<bool> running_{ false };

    std::mutex mtx_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

}  // namespace gomoku
