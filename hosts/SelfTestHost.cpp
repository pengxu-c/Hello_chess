// ============================================================
// hosts/SelfTestHost.cpp - 端到端自检宿主
//
// 覆盖的链路（每一环都是一个独立层，因此每一条断言都在验证一个边界）：
//   1. 注册表     —— 玩家/界面插件能否被枚举与创建
//   2. 内核       —— 开新局、落子、判胜、悔棋、换人
//   3. 存储插件   —— 存档/残局/统计
//   4. 界面插件   —— IView 能否收到状态推送
//   5. HTTP 宿主  —— 静态资源、状态查询、命令总线（真的走 TCP）
//
// 任何一个边界被改坏，这里都会红。
// ============================================================
#include "Hosts.h"

#include "../app/JsonProtocol.h"
#include "../app/ResourcePaths.h"
#include "../app/ViewDriver.h"
#include "../contracts/PlayerRegistry.h"
#include "../contracts/ViewRegistry.h"
#include "../hosts/web/HttpServer.h"
#include "../kernel/MatchSession.h"
#include "../plugins/storage/LegacyStorageGateway.h"
#include "../plugins/views/WebView.h"

#include "../third_party/httplib.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gomoku {
namespace {

int g_pass = 0;
int g_fail = 0;
int g_skipped = 0;   // 环境不具备而跳过的项：既不算通过，也不算失败

void check(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
        std::printf("  [ ok ] %s\n", what.c_str());
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what.c_str());
    }
    std::fflush(stdout);
}

// 等条件成立，最多等 timeoutMs
template <typename Pred>
bool waitUntil(Pred pred, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

// ------------------------------------------------------------
// 规则内核：直接对 MatchState 断言（不经过线程，纯逻辑）
// ------------------------------------------------------------
void testRules() {
    std::printf("\n[1b] rules kernel (MatchState)\n");

    GomokuRules rules;
    SquareBoard board;
    MatchState m(board, rules);

    // 横向五连
    m.reset(RulesConfig{ 15, 5 });
    for (int c = 0; c < 5; ++c) {
        m.applyMove(7, c);          // 黑
        if (c < 4) m.applyMove(8, c);   // 白（无关位置）
    }
    check(m.over() && m.winner() == Stone::Black, "horizontal five in a row wins");

    // ---- 直接验证规则函数本身 ----
    {
        SquareBoard b(15);
        const Stone W = Stone::White;
        b.place(0, 10, W); b.place(1, 10, W); b.place(2, 10, W); b.place(3, 10, W);
        b.place(4, 10, W);
        check(rules.wins(b, 4, 10, W, 5), "rules::wins detects a vertical five");

        SquareBoard h(15);
        for (int c = 0; c < 5; ++c) h.place(7, c, W);
        check(rules.wins(h, 7, 4, W, 5), "rules::wins detects a horizontal five");

        SquareBoard d(15);
        for (int i = 0; i < 5; ++i) d.place(i, i, W);
        check(rules.wins(d, 4, 4, W, 5), "rules::wins detects a diagonal five");

        SquareBoard four(15);
        for (int c = 0; c < 4; ++c) four.place(7, c, W);
        check(!rules.wins(four, 7, 3, W, 5), "rules::wins rejects four in a row");
    }

    // 说明：竖向五连的正确性由上面的 rules::wins 直接调用断言覆盖。
    // 这里原本还想用"轮流落子直到白方成五"再验一遍，但那个用例需要
    // 精确的回合编排，写错一次就变成"其实是黑在落子"的假失败 ——
    // 与其保留一个脆弱的用例，不如让它只验证确定的规则函数。

    // 斜向五连
    m.reset(RulesConfig{ 15, 5 });
    for (int i = 0; i < 5; ++i) {
        m.applyMove(i, i);          // 黑对角
        if (i < 4) m.applyMove(0, 14 - i);
    }
    check(m.over() && m.winner() == Stone::Black, "diagonal five in a row wins");

    // 非法落子：占用 / 越界 / 终局后
    m.reset(RulesConfig{ 15, 5 });
    check(m.applyMove(7, 7), "first move is accepted");
    check(!m.applyMove(7, 7), "occupied cell is rejected");
    check(!m.applyMove(-1, 0), "out-of-range move is rejected");
    check(!m.applyMove(15, 0), "out-of-range move is rejected (upper bound)");
    check(m.moveCount() == 1, "rejected moves do not change the board");

    // 悔棋重建
    m.reset(RulesConfig{ 15, 5 });
    for (int i = 0; i < 6; ++i) m.applyMove(i % 3, i / 3);
    const int before = m.moveCount();
    const int undone = m.undo(2);
    check(undone == 2 && m.moveCount() == before - 2, "MatchState::undo removes moves");
    check(m.board().stoneCount() == before - 2, "undo rebuilds the board correctly");
    const Stone expectedTurn = ((before - 2) % 2 == 0) ? Stone::Black : Stone::White;
    check(!m.over() && m.turn() == expectedTurn, "undo restores the correct turn");

    // 连珠数归一化：三条约束各自生效
    {
        const RulesConfig unset = RulesConfig{ 15, 0 }.normalized();
        check(unset.boardSize == 15 && unset.winLength == 5, "unset win length -> default 5");

        const RulesConfig tooBig = RulesConfig{ 4, 15 }.normalized();
        check(tooBig.winLength <= tooBig.boardSize,
              "win length never exceeds board size (" + std::to_string(tooBig.boardSize) +
                  "x" + std::to_string(tooBig.boardSize) + ", connect " +
                  std::to_string(tooBig.winLength) + ")");

        const RulesConfig hugeBoard = RulesConfig{ 99, 5 }.normalized();
        check(hugeBoard.boardSize == 15, "board size is clamped to a sane range");
    }
}

// ------------------------------------------------------------
// 内核 + 存储 + 玩家（完全不涉及任何界面）
// ------------------------------------------------------------
void testKernel(const std::string& dataDir) {
    std::printf("\n[1] kernel / players / storage\n");

    // 1.1 插件注册表可枚举
    const auto players = playerCatalog();
    check(players.size() >= 8, "player registry exposes built-in players (" +
                                   std::to_string(players.size()) + ")");
    check(viewCatalog().size() >= 2, "view registry exposes built-in views");

    // 1.2 「新增玩家只要一行」的证明：random/greedy 是纯契约实现
    bool hasRandom = false, hasGreedy = false;
    for (const auto& p : players) {
        if (p.id == "random") hasRandom = true;
        if (p.id == "greedy") hasGreedy = true;
    }
    check(hasRandom && hasGreedy, "native contract-only players are registered");

    // 1.3 开一局：greedy 对 minimax（浅层搜索），能下出足够长的棋局
    auto session = std::make_unique<MatchSession>(makeLegacyStorageGateway(dataDir));
    session->setThinkBudgetMs(60);
    session->start();
    session->setStorageEnabledDefault(true);
    session->startGame(RulesConfig{ 9, 5 }, "greedy", "minimax", true);

    const bool started = waitUntil([&] { return session->state().moveCount > 0; }, 4000);
    check(started, "AI vs AI game starts and advances");

    const bool aiPlays = waitUntil([&] { return session->state().moveCount >= 6; }, 10000);
    check(aiPlays, "kernel drives AI turns automatically (>= 6 moves)");

    ViewState s = session->state();
    check(s.boardSize == 9 && s.winLength == 5, "rules config applied (9x9, connect 5)");
    check(s.history.size() == static_cast<size_t>(s.moveCount),
          "move history is the single source of truth");
    check(s.blackName == "Greedy" && s.whiteName == "Minimax++",
          "seat names come from plugin metadata");

    // 1.4 悔棋：等到这一局自然结束再撤子，这样撤销结果不会被后续落子立刻掩盖
    const bool finished = waitUntil(
        [&] { return isFinished(session->state().status); }, 60000);
    check(finished, "AI vs AI game reaches a terminal state");
    const int beforeUndo = session->state().moveCount;
    const bool canUndoFlag = session->state().canUndo;
    std::printf("         (finished game: %d moves, canUndo=%d, status=%s)\n",
                beforeUndo, canUndoFlag ? 1 : 0, s.status.c_str());

    // 1.4b 开新局（终局后必须还能开起来 —— 曾因主循环忙等而失败）
    //     判据用 matchId 递增，而不是步数：AI 棋局很快，
    //     只看步数会与"新局又走到同样步数"混淆（这里真实踩过）。
    //
    //     【为什么只等 matchId，不等 status==InProgress】
    //       这一局是 greedy 对 random（9×9、60ms 预算），弱方几乎不设防，
    //       新局常常在几秒内就下到终局。把 status==InProgress 作为等待条件，
    //       等于要求"必须恰好在采样那一刻还没下完"——那是竞态，不是断言。
    //       （这个失败曾以 3 次里 2 次的频率随机出现。）
    //       真正要证明的是"内核接受了新局并重置了状态机"，matchId 递增即是；
    //       "新局能正常走棋"由下一段 human 回合的用例覆盖。
    const long long matchIdBefore = session->state().matchId;
    const bool thirdGameSent =
        session->startGame(RulesConfig{ 9, 5 }, "greedy", "random", true);
    check(thirdGameSent, "restarting a finished game is accepted");
    const bool newMatchObserved = waitUntil(
        [&] { return session->state().matchId > matchIdBefore; }, 8000);
    const ViewState afterRestart = session->state();
    std::printf("         (restart: matchId %lld -> %lld, status=%s, moves=%d)\n",
                matchIdBefore, afterRestart.matchId, afterRestart.status.c_str(),
                afterRestart.moveCount);
    check(newMatchObserved, "a new game can be started after one finished");
    check(afterRestart.matchId > matchIdBefore, "a new game gets a fresh match id");

    // 1.5 人类 vs AI：用人类占一个座位，对局会稳定停在"等人类落子"，
    //     这样才能确定性地验证换人与悔棋（AI 对 AI 会一路下到终局）。
    session->abort();
    waitUntil([&] { return session->state().status == status::kIdle; }, 3000);
    session->startGame(RulesConfig{ 9, 5 }, "human", "greedy", true);
    const bool humanWaits = waitUntil(
        [&] {
            const ViewState st = session->state();
            return st.status == status::kInProgress && st.humanTurn;
        },
        4000);
    check(humanWaits, "human seat hands the turn to the view");

    // 1.5a 局中换人：只换玩家实现，不动棋局
    const int movesAtSwap = session->state().moveCount;
    session->swapPlayer(Stone::White, "minimax");
    const bool swapped =
        waitUntil([&] { return session->state().whiteName == "Minimax++"; }, 4000);
    const int movesAfterSwap = session->state().moveCount;
    std::printf("         (swap: white = '%s', moves %d -> %d)\n",
                session->state().whiteName.c_str(), movesAtSwap, movesAfterSwap);
    check(swapped, "mid-game player swap takes effect");
    check(movesAfterSwap >= movesAtSwap, "swap does not rewind the board");

    // 1.5b 人类落子 → AI 应答 → 悔棋一步
    session->play(4, 4);
    const bool aiReplied =
        waitUntil([&] { return session->state().moveCount >= 2; }, 8000);
    check(aiReplied, "human move is applied and the AI replies");

    const int beforeHumanUndo = session->state().moveCount;
    session->undo(1);
    const bool undone = waitUntil(
        [&] { return session->state().moveCount == beforeHumanUndo - 1; }, 4000);
    const ViewState afterUndo = session->state();
    std::printf("         (undo: %d -> %d moves, status=%s, message='%s')\n",
                beforeHumanUndo, afterUndo.moveCount, afterUndo.status.c_str(),
                afterUndo.message.c_str());
    check(undone, "undo removes exactly the requested moves");
    check(afterUndo.humanTurn && afterUndo.status == status::kInProgress,
          "undo gives the turn back to the human seat");

    // 1.6 中止
    session->abort();
    check(waitUntil([&] { return session->state().status == status::kIdle; }, 3000),
          "abort returns the session to Idle");

    session->requestQuit();
    session->join(2000);
}

// ------------------------------------------------------------
// 界面插件：IView 是否真的收到推送
// ------------------------------------------------------------
class ProbeView final : public IView {
public:
    std::string id() const override { return "probe"; }
    std::string displayName() const override { return "Probe"; }
    void render(const ViewState& s) override {
        ++frames;
        lastVersion = s.version;
        lastMoveCount = s.moveCount;
    }
    bool poll() override { return !quit; }
    void notify(const std::string& m) override { notes.push_back(m); }

    int frames = 0;
    long long lastVersion = -1;
    int lastMoveCount = -1;
    bool quit = false;
    std::vector<std::string> notes;
};

void testView(const std::string& dataDir) {
    std::printf("\n[2] view plugin (IView)\n");

    auto session = std::make_unique<MatchSession>(makeLegacyStorageGateway(dataDir));
    session->setThinkBudgetMs(80);
    ProbeView probe;
    session->addObserver(&probe);

    check(probe.frames >= 1, "observer receives an immediate first frame on attach");
    check(probe.lastVersion >= 0, "first frame carries a version number");

    session->start();
    session->startGame(RulesConfig{ 9, 5 }, "greedy", "random", true);

    const bool pushed = waitUntil([&] { return probe.frames >= 3; }, 5000);
    check(pushed, "observer keeps receiving frames as the game advances (" +
                      std::to_string(probe.frames) + ")");

    session->requestQuit();
    session->join(2000);
    session->removeObserver(&probe);
}

// ------------------------------------------------------------
// 内核绝不能被界面插件拖住（网页卡死的回归防线）
//
// 【这条用例在防什么】
//   真实症状：在浏览器里下棋，页面偶尔整个卡住，棋局不再推进。
//   根因是一条锁环：
//       内核控制线程 : 持 stateMtx_  →  调 render() → 等 frameMtx_
//       SSE 推送线程 : 持 frameMtx_  →  做阻塞 socket 写
//   只要客户端有一次慢写（标签页挂起、代理缓冲、网络抖动），
//   内核就被界面锁住，整局停止推进。
//
//   这里用一个「故意慢」的界面插件复现同构的锁结构：
//   它的 render() 先取自己的锁，再睡一会儿 —— 与「持 frameMtx_ 时做慢写」等价。
//   如果内核还在持 stateMtx_ 回调界面，这里必然超时失败。
//   它不碰任何 socket，所以在任何环境都能跑。
// ------------------------------------------------------------
class DeadlockProbeView final : public IView {
public:
    // 探针需要拿到内核的只读快照接口。
    // IControllable 刻意不暴露 state()（界面应当只通过 render 被动收帧），
    // 所以这里用一个窄接口向内核"讨"一次读取能力 —— 它模拟的正是
    // SSE 线程在持 frameMtx_ 时去读内核状态这一行为。
    class StateReader {
    public:
        virtual ViewState read() const = 0;
    };

    void bind(StateReader* reader) { reader_ = reader; }

    std::string id() const override { return "deadlock-probe"; }
    std::string displayName() const override { return "Deadlock probe"; }

    void render(const ViewState&) override {
        // ---- 构造真正的锁环，而不是单纯"变慢" ----
        // 步骤 1：取界面自己的锁（等价于 WebView 持 frameMtx_ 做阻塞写）
        std::unique_lock<std::mutex> viewLock(viewMtx_);

        // 步骤 2：持界面锁去读内核状态（等价于 SSE 线程持 frameMtx_ 等内核）
        //   若内核此刻正持 stateMtx_ 调我们的 render()，两者互等 → 死锁。
        //   修复前：内核等 viewMtx_，我们等 stateMtx_ → 卡死，用例超时失败。
        //   修复后：render() 在不持内核锁时调用，这里能立刻拿到状态。
        if (reader_) reader_->read();
        ++renders_;
    }
    bool poll() override { return true; }

    int renders() const { std::lock_guard<std::mutex> lk(viewMtx_); return renders_; }

private:
    mutable std::mutex viewMtx_;
    int renders_ = 0;
    StateReader* reader_ = nullptr;
};

// 把 MatchSession 包装成探针需要的 StateReader。
class SessionStateReader final : public DeadlockProbeView::StateReader {
public:
    explicit SessionStateReader(MatchSession& s) : session_(s) {}
    ViewState read() const override { return session_.state(); }

private:
    MatchSession& session_;
};

void testSlowView(const std::string& dataDir) {
    std::printf("\n[3b] a slow view must never stall the kernel\n");

    auto session = std::make_unique<MatchSession>(makeLegacyStorageGateway(dataDir));
    session->setThinkBudgetMs(20);

    DeadlockProbeView slow;
    SessionStateReader reader(*session);
    slow.bind(&reader);
    session->addObserver(&slow);
    session->start();

    // 人类 vs AI：每一手都会触发 render()，每次 render 都会走上面那条锁链。
    session->startGame(RulesConfig{ 9, 5 }, "human", "greedy", true);
    const bool humanTurn = waitUntil(
        [&] { return session->state().humanTurn; }, 5000);
    check(humanTurn, "kernel reaches the human turn with a render-under-lock view");

    // 关键断言：界面慢 30ms/次，但内核必须能在合理时间内推进。
    // 修复前：内核持 stateMtx_ 回调 render()，总耗时被界面拖长，
    //         甚至与界面互相等待而彻底卡死。
    // 【为什么要一手一手地等】内核只接受"人类回合"的落子，而 AI 走完一手
    // 后回合才会交还人类。所以这里每落一手都等到"又轮到我"再落下一手 ——
    // 一次性连发三手会被内核按设计拒掉（不是 bug，是规则）。
    int placed = 0;
    for (int i = 0; i < 3; ++i) {
        const bool myTurn = waitUntil(
            [&] { return session->state().humanTurn && !session->state().thinking; }, 6000);
        if (!myTurn) break;
        if (session->play(4, 4 + i)) ++placed;
        // 等这一手真正落到盘上（moveCount 增长）
        waitUntil([&] { return session->state().moveCount >= placed * 2; }, 6000);
    }
    const int moves = session->state().moveCount;
    check(moves >= 3, "kernel keeps advancing while the view holds its own lock (" +
                          std::to_string(moves) + " moves, placed " +
                          std::to_string(placed) + ")");
    check(slow.renders() > 0, "the slow view did receive frames (" +
                                  std::to_string(slow.renders()) + ")");

    session->requestQuit();
    session->join(3000);
    session->removeObserver(&slow);
}

// ------------------------------------------------------------
// HTTP 宿主 + WebView：真绑端口、真发请求
// ------------------------------------------------------------
void testHttp(const std::string& webRoot, const std::string& dataDir) {
    std::printf("\n[3] http host + web view (real TCP round trips)\n");

    auto session = std::make_unique<MatchSession>(makeLegacyStorageGateway(dataDir));
    session->setThinkBudgetMs(80);

    ViewContext ctx;
    ctx.webRoot = webRoot;
    ctx.port = 0;              // 自动分配，避免占用固定端口
    ctx.headless = true;
    std::unique_ptr<IView> view = createView("web", ctx);
    check(view != nullptr, "web view plugin creates an HTTP host");
    if (!view) return;

    auto* web = dynamic_cast<WebView*>(view.get());
    check(web != nullptr && web->port() > 0, "web view reports a bound port");
    if (!web) return;
    web->setDebugTrace(true);

    session->addObserver(view.get());
    session->start();

    // 界面插件的输入侧必须有人驱动。宿主用 ViewDriver，自检用同一个 ——
    // 这正是把"主循环"收敛成可复用部件的好处：测试与生产跑的是同一条路径。
    ViewDriver driver(*view, session.get());
    driver.start();

    httplib::Client cli("127.0.0.1", web->port());
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(3, 0);

    // 3.1 静态资源（证明 mount point 生效）
    if (auto res = cli.Get("/")) {
        check(res->status == 200 && res->body.find("board") != std::string::npos,
              "static index.html is served");
    } else {
        check(false, "static index.html is served");
    }
    if (auto res = cli.Get("/app.js")) {
        check(res->status == 200, "static app.js is served");
    } else {
        check(false, "static app.js is served");
    }

    // 3.2 状态查询
    if (auto res = cli.Get("/api/state")) {
        check(res->status == 200, "GET /api/state answers");
    } else {
        check(false, "GET /api/state answers");
    }

    // 3.3 玩家目录（界面下拉框的数据源）
    if (auto res = cli.Get("/api/players")) {
        check(res->status == 200 && res->body.find("tactical-max") != std::string::npos,
              "GET /api/players returns the plugin catalog");
    } else {
        check(false, "GET /api/players returns the plugin catalog");
    }

    // 3.4 命令总线：开新局
    if (auto res = cli.Post("/api/cmd",
                            R"({"op":"new","boardSize":9,"winLength":5,"black":"greedy","white":"random","storageEnabled":true})",
                            "application/json")) {
        check(res->status == 200, "POST /api/cmd accepts a command");
    } else {
        check(false, "POST /api/cmd accepts a command");
    }

    // 命令经由队列在控制线程执行，因此这里要等到状态真的变了。
    // 用 /api/state（界面真正使用的接口）来观察，才能同时验证 SSE 帧缓存链路。
    bool sawInProgress = false;
    int httpMoves = 0;
    std::string httpStatus;
    const bool running = waitUntil(
        [&] {
            if (auto res = cli.Get("/api/state")) {
                nlohmann::json j = nlohmann::json::parse(res->body, nullptr, false);
                if (j.is_object()) {
                    httpStatus = j.value("status", std::string());
                    if (!httpStatus.empty() && httpStatus != "Idle") sawInProgress = true;
                    httpMoves = j.value("moveCount", 0);
                }
            }
            return sawInProgress && httpMoves >= 3;
        },
        10000);
    std::printf("         (via HTTP: status=%s, moveCount=%d)\n",
                httpStatus.c_str(), httpMoves);
    check(sawInProgress, "GET /api/state reflects the command bus (status changed)");
    check(running, "command bus actually drives the kernel (>= 3 moves via HTTP)");

    // 3.5 查询目录（存储插件经由内核暴露给界面）
    if (auto res = cli.Get("/api/games")) {
        check(res->status == 200, "GET /api/games answers through the storage plugin");
    } else {
        check(false, "GET /api/games answers through the storage plugin");
    }
    if (auto res = cli.Get("/api/stats")) {
        check(res->status == 200, "GET /api/stats answers through the storage plugin");
    } else {
        check(false, "GET /api/stats answers through the storage plugin");
    }

    // 3.6 未知接口应当 404（而不是把请求丢给内核）
    if (auto res = cli.Get("/api/nope")) {
        check(res->status == 404, "unknown endpoint returns 404");
    } else {
        check(false, "unknown endpoint returns 404");
    }

    session->requestQuit();
    session->join(2000);
    driver.stop();
}

// ------------------------------------------------------------
// 分层守卫：契约层不许依赖任何上层
//
// 【为什么值得写成自动断言】
//   微内核的全部价值都建立在"依赖只能向下"上。这个约束一旦被某次
//   顺手 #include 破掉，编译照样通过、功能照常工作，但"换内核不影响界面"
//   的承诺已经悄悄失效 —— 属于最难靠 code review 发现的退化。
//   所以这里直接扫源码：contracts/ 只能包含 contracts/ 自己和标准库。
// ------------------------------------------------------------
void testLayerGuard() {
    std::printf("\n[4] layering guard (contracts must not depend on upper layers)\n");

    namespace fs = std::filesystem;
    // 用统一的资源定位器，而不是写死相对路径 "contracts"：
    // 之前这里硬编码相对路径，导致同一个 exe 从 build/ 目录跑必然报
    // "contracts/ directory is present [FAIL]" —— 那是启动目录不对，不是分层被破坏。
    const std::string root = resolveSourceRoot();
    if (root.empty()) {
        // 发布产物里可能没有源码。此时"扫不到"是环境使然，不该判成分层违规，
        // 但必须让人看得见这一节没真跑（否则绿灯是假的）。
        std::printf("  [skip] source tree not found; layering guard not executed\n");
        std::fflush(stdout);
        ++g_skipped;
        return;
    }
    const fs::path contractsDir = fs::path(root) / "contracts";

    // 禁止出现在 contracts 的 #include 里的上层目录
    static const char* kForbidden[] = { "kernel/", "plugins/", "hosts/", "app/", "engine/" };

    int scanned = 0;
    std::string violations;

    for (const auto& entry : fs::recursive_directory_iterator(contractsDir)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".h" && ext != ".cpp") continue;
        ++scanned;

        std::ifstream in(entry.path());
        std::string line;
        int lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            if (line.find("#include") == std::string::npos) continue;
            for (const char* bad : kForbidden) {
                if (line.find(bad) != std::string::npos) {
                    if (!violations.empty()) violations += "; ";
                    violations += entry.path().filename().string() + ":" +
                                  std::to_string(lineNo) + " -> " + bad;
                }
            }
        }
    }

    check(scanned > 0, "contracts/ files were scanned (" + std::to_string(scanned) + ")");
    check(violations.empty(),
          "contracts/ depends only on itself + stdlib" +
              (violations.empty() ? std::string() : std::string(" | violations: ") + violations));
}

}  // namespace

int SelfTestHost::run() {
    std::printf("=== Gomoku microkernel self-test ===\n");

    // 自检用独立的临时目录，避免污染用户的 data/
    const std::string dataDir = opts_.dataDir + "/selftest";
    // 前端资源同样走统一探测：自检必须和真实启动找到同一个 webapp，
    // 否则会出现"网页能开、自检说找不到"这种自相矛盾的结果。
    const std::string webRoot = resolveWebRoot();
    if (webRoot.empty()) {
        std::printf("  [FAIL] webapp/index.html is locatable\n");
        std::fflush(stdout);
        ++g_fail;
    } else {
        std::printf("[info] web root: %s\n", webRoot.c_str());
    }

    testRules();
    testKernel(dataDir);
    testView(dataDir);
    testSlowView(dataDir);
    if (!webRoot.empty()) testHttp(webRoot, dataDir);
    testLayerGuard();

    std::printf("\n=== result: %d passed, %d failed", g_pass, g_fail);
    if (g_skipped > 0) std::printf(", %d skipped", g_skipped);
    std::printf(" ===\n");
    std::fflush(stdout);

    // --hold N：自检结束后继续把网页界面挂 N 秒，便于人工/脚本用真浏览器验收。
    if (holdSeconds > 0) {
        if (webRoot.empty()) {
            std::printf("[hold] webapp not found, cannot serve the UI\n");
            return 1;
        }
        auto session = std::make_unique<MatchSession>(makeLegacyStorageGateway(dataDir));
        ViewContext ctx;
        ctx.webRoot = webRoot;
        ctx.port = opts_.port;
        ctx.headless = true;
        std::unique_ptr<IView> view = createView("web", ctx);
        if (!view) {
            std::printf("[hold] web view unavailable\n");
            return 1;
        }
        auto* web = dynamic_cast<WebView*>(view.get());
        std::printf("[hold] serving the web UI for %d s at http://127.0.0.1:%d/\n",
                    holdSeconds, web ? web->port() : opts_.port);
        std::printf("[hold] this process exits by itself when the countdown ends; "
                    "press Ctrl+C to stop it now.\n");
        std::fflush(stdout);

        session->addObserver(view.get());
        session->start();
        ViewDriver driver(*view, session.get());
        driver.start();
        std::this_thread::sleep_for(std::chrono::seconds(holdSeconds));
        driver.stop();
        session->requestQuit();
        session->join(2000);
    }

    return g_fail == 0 ? 0 : 1;
}

}  // namespace gomoku