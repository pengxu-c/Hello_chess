// ============================================================
// kernel/MatchSession.cpp - 会话实现
//
// 阅读顺序建议：
//   1) 构造函数 / start()      —— 看清楚线程是怎么起来的
//   2) controlLoop()           —— 整个内核唯一的流程主干
//   3) doXxxLocked()           —— 每个命令怎么改状态
//   4) publishLocked()         —— 状态怎么通知到界面插件
// ============================================================
#include "MatchSession.h"
#include "../contracts/IView.h"

#include <algorithm>

namespace gomoku {

MatchSession::MatchSession(StoragePtr storage, PlayerFactory factory)
    : match_(board_, rules_),
      store_(std::move(storage)),
      playerFactory_(std::move(factory)) {
    if (!store_) store_ = makeNullStorageGateway();
    if (!playerFactory_) {
        playerFactory_ = [](const std::string& id, const PlayerContext& ctx) {
            return createPlayer(id, ctx, nullptr);
        };
    }
    store_->setEnabled(storageDefault_);
    // 初始快照：空盘，界面注册后立刻能画出一个正确的空棋盘
    board_.reset(RulesConfig{}.normalized().boardSize);
    match_.reset(RulesConfig{});
    publishLocked();
}

MatchSession::~MatchSession() {
    requestQuit();
    if (controlThread_.joinable()) controlThread_.join();
}

void MatchSession::start() {
    if (controlThread_.joinable()) return;
    finished_.store(false);
    controlThread_ = std::thread([this] { controlLoop(); });
}

void MatchSession::join(int timeoutMs) {
    if (!controlThread_.joinable()) return;
    // 带超时的 join：不阻塞界面主循环。超时则下一帧再试。
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        // std::thread 没有 try_join，这里用 finished_ 标志近似判断
        if (finished_.load()) {
            controlThread_.join();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (finished_.load()) controlThread_.join();
}

void MatchSession::requestQuit() {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        quit_.store(true);
    }
    stateCv_.notify_all();
    workCv_.notify_all();
}

// ============================================================
// 状态快照
// ============================================================
ViewState MatchSession::snapshotLocked() const {
    ViewState s;
    s.status = status_;
    s.matchId = match_.matchId();
    s.boardSize = match_.board().size();
    s.winLength = match_.rules().winLength;
    s.cells = match_.board().toCells();
    s.turn = match_.turn();
    s.moveCount = match_.moveCount();
    s.history = match_.history();

    // 最后一手：分别取黑白各自最近一手，并给出全局最后一手
    for (auto it = s.history.rbegin(); it != s.history.rend(); ++it) {
        if (it->color == Stone::Black && !s.lastBlack.valid()) s.lastBlack = { it->r, it->c };
        if (it->color == Stone::White && !s.lastWhite.valid()) s.lastWhite = { it->r, it->c };
        if (s.lastBlack.valid() && s.lastWhite.valid()) break;
    }
    if (!s.history.empty()) s.lastMove = { s.history.back().r, s.history.back().c };

    s.blackId = blackSeat_.playerId;
    s.blackName = blackSeat_.playerName;
    s.whiteId = whiteSeat_.playerId;
    s.whiteName = whiteSeat_.playerName;

    s.thinking = thinking_;
    s.storageEnabled = store_->enabled();
    s.canUndo = (status_ != status::kIdle) && s.storageEnabled && store_->canUndo();
    s.version = version_;
    s.message = message_;

    IPlayer* cur = playerOnTurnLocked();
    s.humanTurn = (status_ == status::kInProgress) && cur && cur->human();
    return s;
}

ViewState MatchSession::state() const {
    std::lock_guard<std::mutex> lk(stateMtx_);
    return snapshotLocked();
}

long long MatchSession::version() const {
    std::lock_guard<std::mutex> lk(stateMtx_);
    return version_;
}

bool MatchSession::waitForChange(long long& seenVersion, int timeoutMs) {
    std::unique_lock<std::mutex> lk(stateMtx_);
    const bool changed = stateCv_.wait_for(
        lk, std::chrono::milliseconds(timeoutMs),
        [&] { return quit_.load() || version_ != seenVersion; });
    if (quit_.load()) return false;
    if (changed) seenVersion = version_;
    return true;   // 超时也返回 true：交给调用方写心跳，避免连接被中间层掐断
}

// 刷新快照并通知观察者。
//
// 【为什么回调要延后到锁外】
//   本函数被控制线程持有 stateMtx_ 调用。界面的 render() 往往会去拿界面
//   自己的锁（WebView 要 frameMtx_，别的界面可能刷屏/写文件），于是一条
//   危险的锁链就形成了：
//
//       内核线程 : 持 stateMtx_  →  等 frameMtx_（render）
//       SSE 线程 : 持 frameMtx_  →  等 socket 写完成
//
//   只要客户端有一次慢写，内核就被界面拖住，整局对局停止推进 ——
//   也就是"网页下棋卡死"。这不是理论风险，而是真实发生过的现象。
//
//   因此这里只做两件事：在锁内更新状态、把「要发给谁、发什么」攒进
//   pendingFrame_（只留最新一帧，中间帧无意义，天然限流）；
//   真正的界面回调由控制线程在锁外 dispatchNotifications() 里完成。
//   于是无论界面插件多慢，内核都不会被拖住。
void MatchSession::publishLocked() {
    ++version_;
    const ViewState snap = snapshotLocked();
    const bool messageChanged = (message_ != lastNotifiedMessage_);
    lastNotifiedMessage_ = message_;

    stateCv_.notify_all();

    // observers_ 已拷进待发帧：回调发生在锁外，期间观察者列表可能变化，
    // 用快照才不会因为回调中 addObserver/removeObserver 而失效。
    pendingFrame_.snap = snap;
    pendingFrame_.targets = observers_;
    pendingFrame_.hasNotify = messageChanged && !snap.message.empty();
    pendingFrame_.valid = true;
}

// 锁外派发状态帧给界面插件。必须在不持有 stateMtx_ 时调用。
void MatchSession::dispatchNotifications() {
    PendingFrame frame;
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (!pendingFrame_.valid) return;
        frame = std::move(pendingFrame_);
        pendingFrame_ = PendingFrame{};
    }
    // ---- 以下完全不持锁：界面可以任意慢，拖住的只是它自己 ----
    for (IView* v : frame.targets) {
        if (!v) continue;
        v->render(frame.snap);
        if (frame.hasNotify) v->notify(frame.snap.message);
    }
}

// ============================================================
// 观察者注册
// ============================================================
void MatchSession::addObserver(IView* view) {
    if (!view) return;
    ViewState first;
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (std::find(observers_.begin(), observers_.end(), view) == observers_.end())
            observers_.push_back(view);
        first = snapshotLocked();
    }
    view->attach(this);
    view->render(first);   // 注册即同步首帧：界面不用自己写「先拉一次状态」
}

void MatchSession::removeObserver(IView* view) {
    std::lock_guard<std::mutex> lk(stateMtx_);
    observers_.erase(std::remove(observers_.begin(), observers_.end(), view),
                     observers_.end());
}

// ============================================================
// 命令入口（调用方线程）：只投递，不执行
// ============================================================
bool MatchSession::startGame(const RulesConfig& rules,
                             const std::string& blackPlayerId,
                             const std::string& whitePlayerId,
                             bool storageEnabled) {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (quit_.load()) return false;
        pending_.reqNew = true;
        pending_.newRules = rules;
        pending_.newBlack = blackPlayerId;
        pending_.newWhite = whitePlayerId;
        pending_.reqStorage = true;
        pending_.newStorage = storageEnabled;
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

bool MatchSession::play(int r, int c) {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (quit_.load() || status_ != status::kInProgress) return false;
        // 只有人类回合才接受坐标：AI 回合不接受外部落子，避免界面越权
        IPlayer* cur = playerOnTurnLocked();
        if (!cur || !cur->human()) return false;
        pending_.reqPlay = true;
        pending_.playR = r;
        pending_.playC = c;
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

bool MatchSession::undo(int steps) {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (quit_.load() || status_ == status::kIdle) return false;
        pending_.reqUndo += (steps <= 0 ? 1 : steps);
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

bool MatchSession::abort() {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (quit_.load() || status_ == status::kIdle) return false;
        pending_.reqAbort = true;
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

bool MatchSession::swapPlayer(Stone seat, const std::string& playerId) {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (quit_.load() || status_ != status::kInProgress) return false;
        pending_.reqSwap = true;
        pending_.swapSeat = seat;
        pending_.swapId = playerId;
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

bool MatchSession::saveResume(const std::string& note) {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (quit_.load() || status_ == status::kIdle) return false;
        pending_.reqSave = true;
        pending_.saveNote = note;
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

bool MatchSession::loadResume(const std::string& id) {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (quit_.load() || id.empty()) return false;
        pending_.reqLoad = true;
        pending_.loadId = id;
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

bool MatchSession::setStorageEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        pending_.reqStorageToggle = true;
        pending_.storageToggleValue = enabled;
        ++reqGen_;
    }
    workCv_.notify_all();
    return true;
}

// ---- 只读查询：由调用方线程直接执行（存储实现自己保证读操作线程安全） ----
std::vector<StoredBrief> MatchSession::listGames() const {
    std::lock_guard<std::mutex> lk(stateMtx_);
    if (!store_->enabled()) return {};
    return store_->listGames();
}

std::vector<StoredBrief> MatchSession::listResumes() const {
    std::lock_guard<std::mutex> lk(stateMtx_);
    if (!store_->enabled()) return {};
    return store_->listResumeBriefs();
}

std::vector<std::pair<std::string, long long>> MatchSession::stats() const {
    std::lock_guard<std::mutex> lk(stateMtx_);
    return store_->globalStats();
}

// 回放：在一张临时棋盘上重放前 step 手。完全不碰正在进行的对局，
// 因此「开着回放窗口继续下棋」是安全的 —— 这也是把棋盘与界面分开的收益。
ReplayFrame MatchSession::replay(const std::string& id, int step) const {
    ReplayFrame frame;
    std::vector<Move> moves;
    StoredBrief brief;
    {
        std::lock_guard<std::mutex> lk(stateMtx_);
        if (!store_->enabled()) return frame;
        if (!store_->loadMoves(id, moves, brief, /*resume=*/false)) {
            // 残局也能回放：先试棋局，再试残局
            if (!store_->loadMoves(id, moves, brief, /*resume=*/true)) return frame;
        }
    }

    const int total = static_cast<int>(moves.size());
    const int n = (step < 0 || step > total) ? total : step;

    RulesConfig rules;
    rules.boardSize = brief.boardSize;
    rules.winLength = brief.winLength;
    rules = rules.normalized();

    SquareBoard tmp(rules.boardSize);
    MatchState tmpMatch(tmp, rules_);
    tmpMatch.reset(rules);
    for (int i = 0; i < n; ++i) tmpMatch.applyMove(moves[i].r, moves[i].c);

    frame.ok = true;
    frame.boardSize = rules.boardSize;
    frame.winLength = rules.winLength;
    frame.cells = tmp.toCells();
    frame.step = n;
    frame.total = total;
    frame.black = brief.black;
    frame.white = brief.white;
    if (n > 0) {
        frame.last = Coord{ moves[n - 1].r, moves[n - 1].c };
        frame.lastColor = moves[n - 1].color;
    }
    return frame;
}

// ============================================================
// 控制线程主干
// ============================================================
void MatchSession::controlLoop() {
    std::unique_lock<std::mutex> lk(stateMtx_);
    while (!quit_.load()) {
        // ---- 0. 先把攒下的状态帧在锁外派发掉 ----
        // 控制线程全程持有 stateMtx_，而界面 render() 会去拿界面自己的锁。
        // 若在这里直接回调，就形成
        //     内核持 stateMtx_ → 等 frameMtx_   ×   SSE 持 frameMtx_ → 等 socket 写
        // 的锁环，一次慢写就把整局冻住（真实症状：网页下棋卡死）。
        // 所以统一在这里解锁派发：内核逻辑一行不用改，锁语义却彻底正确。
        if (pendingFrame_.valid) {
            lk.unlock();
            dispatchNotifications();
            lk.lock();
            continue;   // 回到顶部重新判断，避免在无锁窗口里继续改状态
        }

        if (!hasWorkLocked()) {
            // 没活干：人类回合等输入，或空闲等开新局。
            // 有超时是为了让 quit_ 能及时生效，不必依赖外部唤醒。
            workCv_.wait_for(lk, std::chrono::milliseconds(50),
                             [this] { return quit_.load() || hasWorkLocked(); });
            continue;
        }

        // ---- 1. 逐条消费命令（用户操作优先于回合推进） ----
        if (pending_.any()) {
            Pending req = pending_;
            pending_.clear();
            handledGen_ = reqGen_;

            if (req.reqNew)            doStartGameLocked(req);
            if (req.reqStorageToggle)  doStorageToggleLocked(req.storageToggleValue);
            if (req.reqLoad)           doLoadLocked(req.loadId);
            if (req.reqUndo > 0)       doUndoLocked(req.reqUndo);
            if (req.reqAbort)          doAbortLocked();
            if (req.reqSwap)           doSwapLocked(req.swapSeat, req.swapId);
            if (req.reqPlay)           doPlayLocked(req.playR, req.playC);
            if (req.reqSave)           doSaveLocked(req.saveNote);
            continue;   // 回到循环顶部重新判断该谁走
        }

        // ---- 2. 回合推进 ----
        if (status_ != status::kInProgress) {
            // 空闲：让出 CPU 后再回到顶部。
            // 必须真的睡一小段 —— 若只等 1ms，一旦出现"没有任何一方需要动作"
            // 的状态，主循环就会退化成忙等，把 CPU 吃满，
            // 并让 startGame / swapPlayer 这类新命令迟迟得不到处理。
            // （这个退化在自检里被真实复现过。）
            workCv_.wait_for(lk, std::chrono::milliseconds(20),
                             [this] { return quit_.load() || pending_.any(); });
            continue;
        }
        IPlayer* cur = playerOnTurnLocked();
        if (!cur) {
            workCv_.wait_for(lk, std::chrono::milliseconds(20),
                             [this] { return quit_.load() || pending_.any(); });
            continue;
        }

        if (cur->human()) {
            // 人类回合：等界面通过 play() 投递坐标。
            // 超时取小值（1ms）是为了让悔棋/换人这类命令几乎立刻被处理；
            // 条件变量带谓词，"没有新命令"时不会真的空转。
            humanAwaiting_ = true;
            workCv_.wait_for(lk, std::chrono::milliseconds(1),
                             [this] { return quit_.load() || pending_.any(); });
            continue;
        }
        humanAwaiting_ = false;

        // AI 回合：交出锁，让 AI 在工作线程里慢慢想
        thinking_ = true;
        publishLocked();
        const int genAtStart = handledGen_;
        const ViewState snapshot = snapshotLocked();
        IPlayer* thinker = cur;
        const ThinkBudget budget{ thinkBudgetMs_, thinkBudgetMs_ <= 0 };
        lk.unlock();
        // 锁外窗口：把"正在思考"这一帧先推给界面（用户要立刻看到自己在等），
        // 然后才让 AI 慢慢算。两者都在无锁状态下发生，互不阻塞。
        dispatchNotifications();
        const Decision d = thinker->tick(snapshot, budget);
        lk.lock();
        thinking_ = false;

        // 思考期间若用户插入了新命令，本次结果作废（否则会出现
        // "刚悔棋又被 AI 立刻补一手" 这类反直觉行为）
        if (quit_.load()) break;
        if (reqGen_ != genAtStart) {
            publishLocked();
            continue;
        }
        if (status_ != status::kInProgress) {
            publishLocked();
            continue;
        }

        if (d.ok) {
            doPlayLocked(d.move.r, d.move.c);
        } else {
            setMessageLocked("AI gave no move: " +
                             (d.reason.empty() ? std::string("no legal move") : d.reason));
            if (status_ == status::kInProgress) {
                // AI 无法落子：判断为放弃，该方判负（工程上比死循环更诚实）
                status_ = (match_.turn() == Stone::Black) ? status::kWhiteWin
                                                          : status::kBlackWin;
                if (store_) store_->finish(status_);
            }
            publishLocked();
        }
    }

    // 退出：结束进行中的记录，把控制线程标记为已结束
    lk.unlock();
    {
        std::lock_guard<std::mutex> g(stateMtx_);
        if (status_ == status::kInProgress && store_->inGame())
            store_->finish(status::kIdle);
        status_ = status::kIdle;
        quit_.store(true);
        ++version_;
        stateCv_.notify_all();
    }
    // 退出前把最后一帧（Idle + 收尾信息）推给界面，锁外派发。
    // 不派发的话，界面上会永远停在最后一手棋的画面（看起来像卡死）。
    {
        std::lock_guard<std::mutex> g(stateMtx_);
        pendingFrame_.snap = snapshotLocked();
        pendingFrame_.targets = observers_;
        pendingFrame_.hasNotify = false;
        pendingFrame_.valid = true;
    }
    dispatchNotifications();
    finished_.store(true);
}

bool MatchSession::hasWorkLocked() const {
    if (quit_.load()) return true;
    if (pending_.any()) return true;
    // 只有"进行中且当前回合方存在"才算有活干。
    // 【重要】终局后必须返回 false：否则主循环会一直认为有事可做而忙等，
    // 既吃满 CPU，又永远走不到"睡在条件变量上"的分支 —— 结果就是
    // 之后投递的 startGame / swapPlayer 等命令虽然进了队列却迟迟不被处理。
    // （这个退化曾在自检里表现为"第二局开不起来"。）
    if (status_ != status::kInProgress) return false;
    return playerOnTurnLocked() != nullptr;
}

// ============================================================
// 命令实现（均在控制线程 + 持有 stateMtx_）
// ============================================================
void MatchSession::doStartGameLocked(const Pending& req) {
    const RulesConfig rules = req.newRules.normalized();
    const bool storageOn = req.reqStorage ? req.newStorage : storageDefault_;

    releasePlayersLocked();
    match_.reset(rules);

    if (store_) {
        store_->setEnabled(storageOn);
    }
    setSeatsLocked(req.newBlack.empty() ? "human" : req.newBlack,
                   req.newWhite.empty() ? "human" : req.newWhite);

    status_ = status::kInProgress;
    message_.clear();
    thinking_ = false;
    humanAwaiting_ = false;

    if (store_ && store_->enabled()) {
        store_->beginGame(rules, blackSeat_, whiteSeat_);
    }

    // 每局开始回调：给玩家插件机会重置内部状态（置换表/统计等）
    {
        const ViewState snap = snapshotLocked();
        if (black_) black_->onMatchStart(snap);
        if (white_) white_->onMatchStart(snap);
    }
    publishLocked();
}

void MatchSession::doPlayLocked(int r, int c) {
    if (status_ != status::kInProgress) return;

    const Stone mover = match_.turn();
    if (!match_.applyMove(r, c)) {
        // 把「为什么不行」原样告诉界面：这是内核给用户的唯一反馈渠道
        if (!board_.inBounds(r, c))          setMessageLocked("Out of range");
        else if (board_.at(r, c) != Stone::Empty) setMessageLocked("Cell occupied");
        else                                  setMessageLocked("Illegal move");
        publishLocked();
        return;
    }

    if (store_ && store_->enabled()) {
        store_->record(Move{ r, c, mover, 0 });
    }
    message_.clear();
    humanAwaiting_ = false;

    if (match_.over()) {
        status_ = statusFromMatch(match_);
        if (store_ && store_->enabled()) store_->finish(status_);
        const ViewState snap = snapshotLocked();
        if (black_) black_->onMatchEnd(snap);
        if (white_) white_->onMatchEnd(snap);
    }
    publishLocked();
}

void MatchSession::doUndoLocked(int steps) {
    if (status_ == status::kIdle) {
        setMessageLocked("No game in progress");
        publishLocked();
        return;
    }
    if (!store_->enabled()) {
        setMessageLocked("Storage is disabled: undo unavailable");
        publishLocked();
        return;
    }
    if (!store_->canUndo()) {
        setMessageLocked("Nothing to undo");
        publishLocked();
        return;
    }

    const int before = store_->moveCount();
    store_->undo(steps, board_);
    const int undone = before - store_->moveCount();
    if (undone <= 0) {
        setMessageLocked("Nothing to undo");
        publishLocked();
        return;
    }

    // 以存储记录为唯一事实来源重建内核状态（不用增量撤销，避免误差累积）
    match_.rebuild(store_->history());

    // 悔棋后的人性化规则：人机对战时把回合交还给人类，让人能改招，
    // 不会出现"撤回 AI 一步后 AI 立刻又下一手"。
    const bool blackHuman = blackSeat_.playerId == "human";
    const bool whiteHuman = whiteSeat_.playerId == "human";
    if (blackHuman != whiteHuman) {
        match_.setTurn(blackHuman ? Stone::Black : Stone::White);
    }

    status_ = status::kInProgress;
    setMessageLocked("Undo " + std::to_string(undone));
    publishLocked();
}

void MatchSession::doAbortLocked() {
    if (status_ == status::kIdle) {
        setMessageLocked("No game in progress");
        publishLocked();
        return;
    }
    if (store_->enabled() && store_->inGame())
        store_->finish(status::kIdle);   // 记录为中止
    status_ = status::kIdle;
    thinking_ = false;
    setMessageLocked("Game aborted");
    publishLocked();
}

void MatchSession::doSwapLocked(Stone seat, const std::string& id) {
    if (status_ != status::kInProgress) return;

    PlayerContext ctx{ &rules_, board_.size(), match_.rules().winLength, thinkBudgetMs_ };
    std::string usedId;
    PlayerPtr fresh = makePlayer(id, &usedId);
    if (!fresh) {
        setMessageLocked("Player unavailable: " + id);
        publishLocked();
        return;
    }

    const std::string display = fresh->displayName();
    if (seat == Stone::Black) {
        black_ = std::move(fresh);
        blackSeat_ = Seat{ usedId, display };
    } else {
        white_ = std::move(fresh);
        whiteSeat_ = Seat{ usedId, display };
    }

    // 只换人不改棋局：棋盘、步数、回合全部保持原样
    setMessageLocked((seat == Stone::Black ? "Black" : "White") +
                     std::string(" player swapped to ") + display);
    publishLocked();
}

void MatchSession::doSaveLocked(const std::string& note) {
    if (!store_->enabled()) {
        setMessageLocked("Storage is disabled");
        publishLocked();
        return;
    }
    const bool ok = store_->saveResume(note, snapshotLocked());
    setMessageLocked(ok ? "Resume saved" : "Save failed");
    publishLocked();
}

void MatchSession::doLoadLocked(const std::string& id) {
    if (!store_->enabled()) {
        setMessageLocked("Storage is disabled");
        publishLocked();
        return;
    }
    RulesConfig rules;
    std::vector<Move> moves;
    std::string blackId, whiteId;
    if (!store_->loadResume(id, rules, moves, blackId, whiteId)) {
        setMessageLocked("Load failed");
        publishLocked();
        return;
    }

    releasePlayersLocked();
    match_.reset(rules);
    setSeatsLocked(blackId.empty() ? "human" : blackId,
                   whiteId.empty() ? "human" : whiteId);

    // 用记录重建棋盘，并把记录重新写入存储（保证存储与棋盘一致）
    store_->beginGame(match_.rules(), blackSeat_, whiteSeat_);
    for (const auto& m : moves) {
        board_.place(m.r, m.c, m.color);
        store_->record(m);
    }
    match_.rebuild(moves);

    status_ = status::kInProgress;
    thinking_ = false;
    setMessageLocked("Resume loaded (" + std::to_string(moves.size()) + " moves)");
    publishLocked();
}

void MatchSession::doStorageToggleLocked(bool on) {
    store_->setEnabled(on);
    setMessageLocked(on ? "Storage enabled" : "Storage disabled");
    publishLocked();
}

// ============================================================
// 玩家与座位
// ============================================================
PlayerPtr MatchSession::makePlayer(const std::string& id, std::string* usedId) {
    const PlayerContext ctx{ &rules_, board_.size(), match_.rules().winLength, thinkBudgetMs_ };
    return playerFactory_(id, ctx);
}

void MatchSession::setSeatsLocked(const std::string& blackId, const std::string& whiteId) {
    const PlayerContext ctx{ &rules_, board_.size(), match_.rules().winLength, thinkBudgetMs_ };

    std::string bUsed = blackId, wUsed = whiteId;
    black_ = playerFactory_(blackId, ctx);
    if (!black_) {                       // 未知/不可用玩家：回退人类，保证一定能开局
        bUsed = "human";
        black_ = playerFactory_("human", ctx);
    }
    white_ = playerFactory_(whiteId, ctx);
    if (!white_) {
        wUsed = "human";
        white_ = playerFactory_("human", ctx);
    }

    blackSeat_ = Seat{ bUsed, black_ ? black_->displayName() : std::string("Unknown") };
    whiteSeat_ = Seat{ wUsed, white_ ? white_->displayName() : std::string("Unknown") };
}

void MatchSession::releasePlayersLocked() {
    black_.reset();
    white_.reset();
}

IPlayer* MatchSession::playerOnTurnLocked() const {
    if (status_ != status::kInProgress) return nullptr;
    return (match_.turn() == Stone::Black) ? black_.get() : white_.get();
}

void MatchSession::setMessageLocked(const std::string& msg) {
    message_ = msg;
}

std::string MatchSession::statusFromMatch(const MatchState& m) {
    if (!m.over()) return status::kInProgress;
    switch (m.winner()) {
        case Stone::Black: return status::kBlackWin;
        case Stone::White: return status::kWhiteWin;
        default:           return status::kDraw;
    }
}

}  // namespace gomoku
