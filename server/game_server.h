// ============================================================
// game_server.h - 本地 HTTP 服务
// 职责：托管前端静态资源、提供 REST 对局接口、用 SSE 推送状态变化。
// 对局状态由 SessionController 统一持有，本类只做协议层转换。
// 线程模型：listen 在独立线程中阻塞运行；请求由 httplib 工作线程处理。
// ============================================================
#pragma once
#include "asset_provider.h"
#include "browser_launcher.h"
#include "session.h"
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace httplib { class Server; }

class GameServer {
public:
    // webRoot：前端资源根目录；port：0 表示自动分配空闲端口
    explicit GameServer(std::string webRoot, int port = 0);
    ~GameServer();

    bool start();                 // 绑定端口并启动监听线程，成功返回 true
    void wait();                  // 阻塞主线程，直到 requestQuit() 被调用
    void stop();                  // 请求退出并回收所有线程
    void requestQuit();           // 请求退出（可由 /api/quit 或外部触发）
    int  port() const { return port_; }   // 实际监听端口
    std::string url() const;              // 访问地址

private:
    std::string webRoot_;                          // 前端资源根目录
    int port_ = 0;                                 // 实际监听端口
    std::unique_ptr<httplib::Server> svr_;         // HTTP 服务实例
    std::unique_ptr<AssetProvider> assets_;        // 静态资源提供者
    std::unique_ptr<SessionController> session_;   // 对局状态机
    std::thread listenThread_;                     // 监听线程

    std::mutex quitMtx_;
    std::condition_variable quitCv_;
    std::atomic<bool> stop_{false};

    void registerRoutes();                 // 注册所有路由
};