// ============================================================
// plugins/views/WebView.cpp - 浏览器界面实现
//
// 数据流（单向，界面永远改不了棋局）：
//     内核控制线程 ──render()──▶ 最新 JSON 帧 ──SSE──▶ 浏览器
//     浏览器 ──POST /api/cmd──▶ 命令队列 ──poll()──▶ IControllable（内核）
//
// 注意 poll() 是「把队列里的命令转发给内核」，不是「等待网络」。
// 因此网络再慢也只影响这一帧的响应延迟，不会阻塞 AI 思考或别人拉状态。
// ============================================================
#include "WebView.h"

#include "../hosts/web/HttpServer.h"
#include "../../app/JsonProtocol.h"
#include "../../contracts/PlayerRegistry.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace gomoku {
namespace {

HttpReply jsonReply(const nlohmann::json& j, int status = 200) {
    HttpReply r;
    r.status = status;
    r.contentType = "application/json; charset=utf-8";
    r.body = j.dump();
    return r;
}

// 从 JSON body 里安全取值（body 可能为空或非法）
nlohmann::json parseBody(const std::string& body) {
    if (body.empty()) return nlohmann::json::object();
    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nlohmann::json::object();
    return j;
}

}  // namespace

WebView::WebView(std::string webRoot, int port, bool openBrowser, bool headless)
    : webRoot_(std::move(webRoot)), port_(port),
      openBrowser_(openBrowser), headless_(headless) {}

WebView::~WebView() {
    done_.store(true);
    if (server_) server_->stop();
}

bool WebView::start() {
    server_ = std::make_unique<HttpServer>(webRoot_, port_);

    server_->onRequest([this](const std::string& m, const std::string& p,
                              const std::string& b) { return handleRequest(m, p, b); });

    server_->onStream({ "/api/events" }, [this](IStream& stream) {
        // 先占一个订阅名额，拿不到就直接断开：宁可让浏览器重连，
        // 也不能让堆积的 SSE 长连接把 httplib 的线程池吃光，
        // 否则连 POST /api/cmd 都会排队等不到工作线程。
        {
            const int now = subscriberCount_.fetch_add(1);
            if (now >= kMaxSubscribers) {
                subscriberCount_.fetch_sub(1);
                if (debugTrace_)
                    std::fprintf(stderr,
                                 "[web] SSE refused: %d subscribers already active\n",
                                 now);
                stream.push(": too many subscribers\n\n");
                return;   // 立刻结束，释放这个工作线程
            }
        }
        SubscriberGuard guard(subscriberCount_);

        // 每条连接独立记录已推送版本：-1 保证首帧立刻送出
        long long seen = -1;
        while (!done_.load()) {
            // 【关键：网络写必须在锁外】
            //   stream.push() 是真实的 socket 写，会阻塞。若把它放在 frameMtx_ 里，
            //   一次慢写（浏览器标签页挂起、代理缓冲、网络抖动）就会长时间占住锁，
            //   而内核控制线程的 render() 正在等这把锁 —— 结果是整局对局停止推进，
            //   表现为"网页下棋卡死"。
            //
            //   做法：在锁内只把「要发什么」取出来（拷贝），出锁后再写网络。
            //   这样内核永远不会被客户端的网络状况拖住。
            std::string payload;
            bool havePayload = false;
            {
                std::unique_lock<std::mutex> lk(frameMtx_);
                frameCv_.wait_for(lk, std::chrono::seconds(15), [this, seen] {
                    return done_.load() || (haveFrame_ && frameVersion_ != seen);
                });
                if (done_.load()) break;
                if (!haveFrame_ || frameVersion_ == seen) {
                    havePayload = false;      // 超时：发心跳
                } else {
                    seen = frameVersion_;
                    payload = "data: " + lastFrameJson_ + "\n\n";
                    havePayload = true;
                }
            }
            // ---- 以下都不持有 frameMtx_ ----
            const bool ok = havePayload ? stream.push(payload)
                                        : stream.push(": ping\n\n");
            if (!ok) break;
        }
    });

    if (!server_->start()) return false;
    port_ = server_->port();
    return true;
}

std::string WebView::url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/";
}

// ---- 内核 → 界面：缓存状态帧并唤醒所有 SSE 连接 ----
void WebView::render(const ViewState& state) {
    const std::string frame = toJson(state).dump();
    {
        std::lock_guard<std::mutex> lk(frameMtx_);
        lastFrameJson_ = frame;
        frameVersion_ = state.version;
        haveFrame_ = true;
    }
    frameCv_.notify_all();
}

void WebView::notify(const std::string& message) {
    if (message.empty()) return;
    // 提示走状态帧里的 message 字段（前端自己弹 toast），这里只记录到控制台
    if (!headless_) std::printf("[session] %s\n", message.c_str());
}

// ---- 界面 → 内核：把 HTTP 线程排队的命令逐条转发 ----
bool WebView::poll() {
    if (done_.load()) return false;
    if (!session_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return true;
    }

    for (int guard = 0; guard < 64; ++guard) {   // 一帧最多处理 64 条，避免饿死其它工作
        std::string cmd;
        {
            std::unique_lock<std::mutex> lk(cmdMtx_);
            if (commands_.empty()) {
                cmdCv_.wait_for(lk, std::chrono::milliseconds(5),
                                [this] { return done_.load() || !commands_.empty(); });
            }
            if (done_.load()) return false;
            if (commands_.empty()) break;
            cmd = std::move(commands_.front());
            commands_.pop_front();
        }
        dispatchCommand(cmd);
    }
    return !done_.load();
}

// 把一条 JSON 命令翻译成对 IControllable 的调用。
// 这是整个 Web 界面里唯一「知道命令语义」的地方 —— 前端只发字符串。
void WebView::dispatchCommand(const std::string& raw) {
    nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;

    const std::string op = j.value("op", std::string());
    if (debugTrace_) std::fprintf(stderr, "[web] dispatch op=%s\n", op.c_str());
    if (op == "new") {
        const NewGameRequest req = parseNewGame(j);
        session_->startGame(req.rules, req.black, req.white, req.storageEnabled);
    } else if (op == "move") {
        session_->play(j.value("r", -1), j.value("c", -1));
    } else if (op == "undo") {
        session_->undo(j.value("steps", 1));
    } else if (op == "abort") {
        session_->abort();
    } else if (op == "swap") {
        const int seat = j.value("seat", 0);
        session_->swapPlayer(seat == 0 ? Stone::Black : Stone::White,
                             j.value("playerId", std::string("human")));
    } else if (op == "save") {
        session_->saveResume(j.value("note", std::string()));
    } else if (op == "load") {
        session_->loadResume(j.value("id", std::string()));
    } else if (op == "storage") {
        session_->setStorageEnabled(j.value("enabled", true));
    } else if (op == "quit") {
        done_.store(true);
        if (session_) session_->requestQuit();
    }
}

void WebView::pushCommand(const std::string& json) {
    {
        std::lock_guard<std::mutex> lk(cmdMtx_);
        commands_.push_back(json);
    }
    cmdCv_.notify_all();
}

// ============================================================
// 请求分发：接口只有「命令」与「查询」两类
// ============================================================
HttpReply WebView::handleRequest(const std::string& method, const std::string& path,
                                 const std::string& body) {
    // 静态资源由 HttpServer 的 mount point 处理，走不到这里；
    // 走到这里说明是 API 路径或 404。
    if (!session_) return jsonReply(errJson("session not ready"), 503);

    // ---- 查询（GET） ----
    if (path == "/api/state") {
        std::lock_guard<std::mutex> lk(frameMtx_);
        if (!haveFrame_) return jsonReply(nlohmann::json::object());
        return jsonReply(nlohmann::json::parse(lastFrameJson_, nullptr, false));
    }
    if (path == "/api/players") {
        return jsonReply(playerCatalogJson(playerCatalog()));
    }
    if (path == "/api/games") {
        return jsonReply(storedListJson(session_->listGames()));
    }
    if (path == "/api/resumes") {
        return jsonReply(storedListJson(session_->listResumes()));
    }
    if (path == "/api/stats") {
        nlohmann::json j = nlohmann::json::object();
        for (const auto& kv : session_->stats()) j[kv.first] = kv.second;
        return jsonReply(j);
    }

    // ---- 命令（POST） ----
    if (method == "POST" && path == "/api/cmd") {
        if (body.empty()) return jsonReply(errJson("empty body"), 400);
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return jsonReply(errJson("bad json"), 400);
        pushCommand(body);
        return jsonReply(okJson());
    }
    if (method == "POST" && path == "/api/replay") {
        const nlohmann::json b = parseBody(body);
        const ReplayFrame f = session_->replay(b.value("id", std::string()),
                                               b.value("step", -1));
        nlohmann::json j;
        j["ok"] = f.ok;
        if (f.ok) {
            j["boardSize"] = f.boardSize;
            j["winLength"] = f.winLength;
            j["cells"] = f.cells;
            j["step"] = f.step;
            j["total"] = f.total;
            j["last"] = { f.last.r, f.last.c };
            j["lastColor"] = static_cast<int>(f.lastColor);
            j["black"] = f.black;
            j["white"] = f.white;
        }
        return jsonReply(j, f.ok ? 200 : 404);
    }

    return jsonReply(errJson("unknown endpoint: " + path), 404);
}

}  // namespace gomoku
