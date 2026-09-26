// ============================================================
// game_server.cpp - 本地 HTTP 服务实现
// 路由：/api/state、/api/events(SSE)、/api/players、/api/newgame、
//       /api/move、/api/undo、/api/save、/api/resumes、/api/load、
//       /api/games、/api/replay、/api/stats、/api/quit，以及静态资源兜底。
// 端口默认自动分配，listen 放到独立线程，主线程可继续做别的事。
// ============================================================
#include "game_server.h"
#include "httplib.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 统一 JSON 响应
static void sendJson(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json; charset=utf-8");
}

GameServer::GameServer(std::string webRoot, int port)
    : webRoot_(std::move(webRoot)),
      port_(port),
      svr_(std::make_unique<httplib::Server>()),
      assets_(std::make_unique<DiskAssetProvider>(webRoot_)),
      session_(std::make_unique<SessionController>()) {}

GameServer::~GameServer() {
    stop();
}

std::string GameServer::url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/";
}

void GameServer::registerRoutes() {
    // ---- 状态查询：供首次拉取与调试（SSE 的兜底） ----
    svr_->Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, session_->snapshot().toJson());
    });

    // ---- SSE：状态变化时推送一帧 JSON ----
    svr_->Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        // 每条连接独立记录已推送版本（-1 保证首帧立即推送）
        auto seen = std::make_shared<long long>(-1);
        res.set_chunked_content_provider(
            "text/event-stream",
            [this, seen](size_t, httplib::DataSink& sink) -> bool {
                if (!session_->waitForChange(*seen)) return false;   // 退出
                std::string payload = "data: " + session_->snapshot().toJson().dump() + "\n\n";
                return sink.write(payload.data(), payload.size());
            },
            [](bool) {});
    });

    // ---- 棋手目录 ----
    svr_->Get("/api/players", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, SessionController::playerCatalog());
    });

    // ---- 开新局 ----
    svr_->Post("/api/newgame", [this](const httplib::Request& req, httplib::Response& res) {
        json b = json::parse(req.body, nullptr, false);
        if (b.is_discarded()) {
            sendJson(res, json{ { "ok", false }, { "error", "bad json" } }, 400);
            return;
        }
        session_->newGame(b.value("boardSize", 15),
                          b.value("winLength", 5),
                          b.value("p1Type", 1),
                          b.value("p2Type", 2),
                          b.value("storageEnabled", false));
        sendJson(res, json{ { "ok", true } });
    });

    // ---- 人类落子 ----
    svr_->Post("/api/move", [this](const httplib::Request& req, httplib::Response& res) {
        json b = json::parse(req.body, nullptr, false);
        if (b.is_discarded() || !b.contains("r") || !b.contains("c")) {
            sendJson(res, json{ { "ok", false }, { "error", "need r,c" } }, 400);
            return;
        }
        session_->humanMove(b.value("r", -1), b.value("c", -1));
        sendJson(res, json{ { "ok", true } });
    });

    // ---- 悔棋 ----
    svr_->Post("/api/undo", [this](const httplib::Request& req, httplib::Response& res) {
        json b = json::parse(req.body, nullptr, false);
        int n = b.is_discarded() ? 1 : b.value("n", 1);
        session_->undo(n);
        sendJson(res, json{ { "ok", true } });
    });

    // ---- 存残局 ----
    svr_->Post("/api/save", [this](const httplib::Request& req, httplib::Response& res) {
        json b = json::parse(req.body, nullptr, false);
        std::string note = b.is_discarded() ? "" : b.value("note", "");
        bool ok = session_->saveResume(note);
        sendJson(res, json{ { "ok", ok } });
    });

    // ---- 残局列表 ----
    svr_->Get("/api/resumes", [this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, session_->listResumes());
    });

    // ---- 载入残局 ----
    svr_->Post("/api/load", [this](const httplib::Request& req, httplib::Response& res) {
        json b = json::parse(req.body, nullptr, false);
        std::string id = b.is_discarded() ? "" : b.value("id", "");
        if (id.empty()) { sendJson(res, json{ { "ok", false } }, 400); return; }
        session_->loadResume(id);
        sendJson(res, json{ { "ok", true } });
    });

    // ---- 棋局列表 ----
    svr_->Get("/api/games", [this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, session_->listGames());
    });

    // ---- 回放定位 ----
    svr_->Post("/api/replay", [this](const httplib::Request& req, httplib::Response& res) {
        json b = json::parse(req.body, nullptr, false);
        std::string id = b.is_discarded() ? "" : b.value("id", "");
        int step = b.is_discarded() ? -1 : b.value("step", -1);
        sendJson(res, session_->replay(id, step));
    });

    // ---- 全局统计 ----
    svr_->Get("/api/stats", [this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, session_->globalStats());
    });

    // ---- 退出 ----
    svr_->Post("/api/quit", [this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, json{ { "ok", true } });
        requestQuit();
    });

    // ---- 前端静态资源：兜底路由，从 web/ 目录读取 ----
    svr_->Get(R"(/.*)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string content, mime;
        if (assets_->get(req.path, content, mime)) {
            res.set_content(content, mime);
        } else {
            res.status = 404;
            res.set_content("404 Not Found: " + req.path,
                            "text/plain; charset=utf-8");
        }
    });
}

bool GameServer::start() {
    // 启动自检：web/index.html 缺失则直接失败，便于排查
    if (!assets_->ready()) return false;

    registerRoutes();

    // 指定端口则绑定指定端口，否则自动分配空闲端口
    int bound = 0;
    if (port_ > 0) {
        bound = svr_->bind_to_port("127.0.0.1", port_) ? port_ : -1;
    } else {
        bound = svr_->bind_to_any_port("127.0.0.1");
    }
    if (bound <= 0) return false;
    port_ = bound;

    listenThread_ = std::thread([this] { svr_->listen_after_bind(); });
    return true;
}

void GameServer::wait() {
    std::unique_lock<std::mutex> lk(quitMtx_);
    quitCv_.wait(lk, [this] { return stop_.load(); });
}

void GameServer::requestQuit() {
    {
        std::lock_guard<std::mutex> lk(quitMtx_);
        stop_.store(true);
    }
    quitCv_.notify_all();
    if (session_) session_->requestQuit();
}

void GameServer::stop() {
    requestQuit();
    if (svr_) svr_->stop();
    if (listenThread_.joinable()) listenThread_.join();
    if (session_) session_->join();
}