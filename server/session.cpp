// ============================================================
// session.cpp - Web 模式对局状态机实现
// loop 线程独占棋盘与玩家，串行推进回合；
// AI 落子在锁外计算，避免阻塞快照读取与 SSE 推送。
// ============================================================
#include "session.h"
#include "player.h"
#include "ai_player.h"
#include "experimental_tactical_player.h"
#include <algorithm>

using json = nlohmann::json;

// 棋手编号 → 显示名（人类固定为 Human）
static std::string playerNameOf(int choice) {
    switch (choice) {
        case 1: return "Human";
        case 2: return "EasyJudge";
        case 3: return "PureGreed 1.0";
        case 4: return "PureGreed 1.1";
        case 5: return "Minimax++";
        case 6: return "API AI";
        case 7: return "Tactical++";
        default: return "Human";
    }
}

// 有效棋手编号（1..7）
static bool validChoice(int c) { return c >= 1 && c <= 7; }

// ---------- SessionSnapshot ----------
json SessionSnapshot::toJson() const {
    json j;
    j["status"] = status;
    j["boardSize"] = boardSize;
    j["winLength"] = winLength;
    j["cells"] = cells;
    j["turn"] = turn;
    j["lastBlack"] = json::array({ lastBlackR, lastBlackC });
    j["lastWhite"] = json::array({ lastWhiteR, lastWhiteC });
    j["moveCount"] = moveCount;
    j["canUndo"] = canUndo;
    j["thinking"] = thinking;
    j["humanTurn"] = humanTurn;
    j["players"] = { { "black", blackName }, { "white", whiteName } };
    j["message"] = message;
    return j;
}

// ---------- 生命周期 ----------
SessionController::SessionController() {
    aiConfig_.loadFromFile();
    loop_ = std::thread([this] { runLoop(); });
}

SessionController::~SessionController() {
    requestQuit();
    join();
    releasePlayers();
}

void SessionController::join() {
    if (loop_.joinable()) loop_.join();
}

void SessionController::releasePlayers() {
    delete p1_;
    delete p2_;
    p1_ = p2_ = nullptr;
}

// 人类(1) 返回 nullptr：其落子由 HTTP /api/move 提供，不经过 Player::place
Player* SessionController::createPlayer(int choice) {
    switch (choice) {
        case 1: return nullptr;
        case 2: return new EasyJudgeAI();
        case 3: return new GreedyScoringAI(0.0, "PureGreed 1.0");
        case 4: return new GreedyScoringAI(1.0, "PureGreed 1.1");
        case 5: return new MinimaxPP(judge_);
        case 6:
            if (aiConfig_.enabled) return new APIPlayer(aiConfig_);
            return new MinimaxPP(judge_);      // 未配置则回退 Minimax++
        case 7: return new TacticalPP(judge_);
        default: return nullptr;               // 非法编号按人类处理
    }
}

// ---------- HTTP 入口 ----------
void SessionController::newGame(int bs, int wl, int p1t, int p2t, bool storageEnabled) {
    std::lock_guard<std::mutex> lk(mtx_);
    reqNew_ = true;
    newBs_ = bs; newWl_ = wl;
    newP1_ = validChoice(p1t) ? p1t : 1;
    newP2_ = validChoice(p2t) ? p2t : 2;
    newStorage_ = storageEnabled;
    cv_.notify_all();
}

void SessionController::humanMove(int r, int c) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (status_ != "InProgress") return;
    int cur = (turn_ == ChessType::Black) ? p1Type_ : p2Type_;
    if (cur != 1) return;                      // 不是人类回合，忽略
    moveR_ = r; moveC_ = c; reqMove_ = true;
    cv_.notify_all();
}

void SessionController::undo(int n) {
    std::lock_guard<std::mutex> lk(mtx_);
    reqUndo_ = (n <= 0) ? 1 : n;
    cv_.notify_all();
}

void SessionController::loadResume(const std::string& id) {
    std::lock_guard<std::mutex> lk(mtx_);
    reqLoad_ = true;
    loadId_ = id;
    cv_.notify_all();
}

void SessionController::requestQuit() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        quit_.store(true);
    }
    cv_.notify_all();
}

SessionSnapshot SessionController::snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return snap_;
}

long long SessionController::version() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return version_;
}

// 阻塞等待版本变化；最多 15 秒超时（超时也返回 true，作为 SSE 心跳）
bool SessionController::waitForChange(long long& seen) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait_for(lk, std::chrono::seconds(15),
                 [this, seen] { return quit_.load() || version_ != seen; });
    if (quit_.load()) return false;
    seen = version_;
    return true;
}

// ---------- 存储 / 统计 ----------
json SessionController::listGames() const {
    std::lock_guard<std::mutex> lk(mtx_);
    json arr = json::array();
    for (const auto& id : storage_.listGames()) {
        GameRecord r;
        if (storage_.loadGame(id, r)) {
            arr.push_back({ { "id", id },
                            { "boardSize", r.boardSize },
                            { "winLength", r.winLength },
                            { "black", r.player1Name },
                            { "white", r.player2Name },
                            { "moves", (int)r.moves.size() } });
        }
    }
    return arr;
}

json SessionController::listResumes() const {
    std::lock_guard<std::mutex> lk(mtx_);
    json arr = json::array();
    for (const auto& id : storage_.listResumes()) {
        GameRecord r;
        if (storage_.loadResume(id, r)) {
            arr.push_back({ { "id", id },
                            { "boardSize", r.boardSize },
                            { "winLength", r.winLength },
                            { "black", r.player1Name },
                            { "white", r.player2Name },
                            { "moves", (int)r.moves.size() },
                            { "note", r.note } });
        }
    }
    return arr;
}

json SessionController::globalStats() const {
    std::lock_guard<std::mutex> lk(mtx_);
    GlobalStats s = storage_.globalStats();
    return json{
        { "totalGames", s.totalGames }, { "blackWins", s.blackWins },
        { "whiteWins", s.whiteWins },   { "draws", s.draws },
        { "aborts", s.aborts },         { "blackTotalMoves", s.blackTotalMoves },
        { "whiteTotalMoves", s.whiteTotalMoves }
    };
}

bool SessionController::saveResume(const std::string& note) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (status_ == "Idle") return false;
    return storage_.saveResume(note);
}

// 回放：在临时棋盘上重放，不影响当前对局
json SessionController::replay(const std::string& id, int step) const {
    std::lock_guard<std::mutex> lk(mtx_);
    GameRecord rec;
    if (!storage_.loadGame(id, rec)) return json{ { "ok", false } };

    int total = static_cast<int>(rec.moves.size());
    int n = (step < 0) ? total : std::min(step, total);

    Board b;
    b.resize(rec.boardSize);
    b.setWinLen(rec.winLength);
    b.clear();
    for (int i = 0; i < n; i++)
        b.place(rec.moves[i].r, rec.moves[i].c, rec.moves[i].color);

    std::vector<int> cells(static_cast<size_t>(rec.boardSize) * rec.boardSize, 0);
    for (int r = 0; r < rec.boardSize; r++)
        for (int c = 0; c < rec.boardSize; c++)
            cells[static_cast<size_t>(r) * rec.boardSize + c] = static_cast<int>(b.at(r, c));

    json j;
    j["ok"] = true;
    j["boardSize"] = rec.boardSize;
    j["cells"] = cells;
    j["step"] = n;
    j["total"] = total;
    j["black"] = rec.player1Name;
    j["white"] = rec.player2Name;
    if (n > 0) {
        j["last"] = json::array({ rec.moves[n - 1].r, rec.moves[n - 1].c });
        j["lastColor"] = static_cast<int>(rec.moves[n - 1].color);
    }
    return j;
}

json SessionController::playerCatalog() {
    json arr = json::array();
    for (int i = 1; i <= 7; i++) {
        json p;
        p["id"] = i;
        p["name"] = playerNameOf(i);
        p["isHuman"] = (i == 1);
        arr.push_back(p);
    }
    return arr;
}

// ---------- loop 线程 ----------
void SessionController::runLoop() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (!quit_.load()) {
        // 1) 新局
        if (reqNew_) { reqNew_ = false; startNewGameLocked(); continue; }
        // 2) 载入残局
        if (reqLoad_) { reqLoad_ = false; loadResumeLocked(loadId_); continue; }
        // 3) 悔棋
        if (reqUndo_ > 0) { int n = reqUndo_; reqUndo_ = 0; undoLocked(n); continue; }

        // 4) 未在对局中：等待新局/载入
        if (status_ != "InProgress") {
            cv_.wait(lk, [this] { return quit_.load() || reqNew_ || reqLoad_; });
            continue;
        }

        // 5) 判断当前该谁走
        int cur = (turn_ == ChessType::Black) ? p1Type_ : p2Type_;
        if (cur == 1) {
            // 人类回合：等待 HTTP 投递坐标
            if (!reqMove_) {
                cv_.wait(lk, [this] {
                    return quit_.load() || reqMove_ || reqNew_ || reqLoad_ || reqUndo_ > 0;
                });
                continue;
            }
            int r = moveR_, c = moveC_;
            reqMove_ = false;
            applyMoveLocked(r, c);
        } else {
            // AI 回合：在锁外计算，避免阻塞快照读取
            Player* pl = (turn_ == ChessType::Black) ? p1_ : p2_;
            ChessType t = turn_;
            thinking_ = true;
            publishLocked();
            lk.unlock();

            Pos p{ -1, -1 };
            if (pl) p = pl->place(board_, t);

            lk.lock();
            thinking_ = false;
            // 若思考期间用户请求了悔棋/新局/载入，则丢弃本次 AI 落子，
            // 交给下一轮循环处理，避免"悔棋后又立刻被 AI 补一手"。
            bool interrupted = reqUndo_ > 0 || reqNew_ || reqLoad_;
            if (!quit_.load() && status_ == "InProgress" && p.valid() && !interrupted) {
                applyMoveLocked(p.r, p.c);
            } else {
                publishLocked();
            }
        }
    }
}

void SessionController::startNewGameLocked() {
    int bs = newBs_, wl = newWl_;
    if (wl < 4 || wl > 15) wl = 5;
    if (bs < wl || bs > 30) bs = 15;

    board_.resize(bs);
    board_.setWinLen(wl);
    board_.clear();

    p1Type_ = newP1_;
    p2Type_ = newP2_;
    releasePlayers();
    p1_ = createPlayer(p1Type_);
    p2_ = createPlayer(p2Type_);

    turn_ = ChessType::Black;
    lastBlack_ = { -1, -1 };
    lastWhite_ = { -1, -1 };
    status_ = "InProgress";
    thinking_ = false;
    message_.clear();
    history_.clear();

    storage_.setEnabled(newStorage_);
    storage_.startGame(bs, wl, playerNameOf(p1Type_), playerNameOf(p2Type_),
                       p1Type_, p2Type_);
    publishLocked();
}

void SessionController::applyMoveLocked(int r, int c) {
    if (!board_.inBounds(r, c)) { message_ = "Out of range"; publishLocked(); return; }
    if (board_.at(r, c) != ChessType::None) { message_ = "Cell occupied"; publishLocked(); return; }

    board_.place(r, c, turn_);
    history_.push_back({ { r, c }, turn_ });
    if (turn_ == ChessType::Black) lastBlack_ = { r, c };
    else                           lastWhite_ = { r, c };
    storage_.recordMove(r, c, turn_);
    message_.clear();

    if (judge_.checkWin(board_, { r, c }, turn_)) {
        status_ = (turn_ == ChessType::Black) ? "BlackWin" : "WhiteWin";
        storage_.endGame(turn_ == ChessType::Black ? GameStatus::BlackWin : GameStatus::WhiteWin);
    } else if (board_.isFull()) {
        status_ = "Draw";
        storage_.endGame(GameStatus::Draw);
    } else {
        turn_ = opponent(turn_);
    }
    publishLocked();
}

void SessionController::undoLocked(int n) {
    if (status_ == "Idle") { message_ = "No game in progress"; publishLocked(); return; }
    if (history_.empty()) { message_ = "Nothing to undo"; publishLocked(); return; }
    if (n > static_cast<int>(history_.size())) n = static_cast<int>(history_.size());

    for (int i = 0; i < n; i++) history_.pop_back();

    // 重建棋盘
    board_.clear();
    for (const auto& h : history_) board_.place(h.first.r, h.first.c, h.second);

    // 重建最后一手标记
    lastBlack_ = { -1, -1 };
    lastWhite_ = { -1, -1 };
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        if (it->second == ChessType::Black && !lastBlack_.valid()) lastBlack_ = it->first;
        if (it->second == ChessType::White && !lastWhite_.valid()) lastWhite_ = it->first;
        if (lastBlack_.valid() && lastWhite_.valid()) break;
    }

    // 悔棋后强制轮到人类方：人机对战时人类可重新选点（改招），
    // 不会出现"撤回 AI 一步后 AI 立刻又下"的情况。
    // 双方均为人类（或均为 AI）时按正常轮次（步数奇偶）。
    bool p1Human = (p1Type_ == 1);
    bool p2Human = (p2Type_ == 1);
    if (p1Human && !p2Human)       turn_ = ChessType::Black;
    else if (!p1Human && p2Human)  turn_ = ChessType::White;
    else                           turn_ = (history_.size() % 2 == 0) ? ChessType::Black : ChessType::White;

    status_ = "InProgress";
    message_ = "Undo " + std::to_string(n);

    // 存储同步：结束旧记录并按剩余历史重建
    if (storage_.isEnabled()) {
        storage_.endGame(GameStatus::Aborted);
        storage_.startGame(board_.size(), board_.winLen(),
                           playerNameOf(p1Type_), playerNameOf(p2Type_), p1Type_, p2Type_);
        for (const auto& h : history_)
            storage_.recordMove(h.first.r, h.first.c, h.second);
    }
    publishLocked();
}

void SessionController::loadResumeLocked(const std::string& id) {
    GameRecord rec;
    if (!storage_.loadResume(id, rec)) { message_ = "Load failed"; publishLocked(); return; }

    int bs = rec.boardSize, wl = rec.winLength;
    if (wl < 4 || wl > 15) wl = 5;
    if (bs < wl || bs > 30) bs = 15;

    board_.resize(bs);
    board_.setWinLen(wl);
    board_.clear();
    history_.clear();
    for (const auto& m : rec.moves) {
        board_.place(m.r, m.c, m.color);
        history_.push_back({ { m.r, m.c }, m.color });
    }

    p1Type_ = validChoice(rec.player1Type) ? rec.player1Type : 1;
    p2Type_ = validChoice(rec.player2Type) ? rec.player2Type : 1;
    releasePlayers();
    p1_ = createPlayer(p1Type_);
    p2_ = createPlayer(p2Type_);

    turn_ = (history_.size() % 2 == 0) ? ChessType::Black : ChessType::White;
    lastBlack_ = { -1, -1 };
    lastWhite_ = { -1, -1 };
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        if (it->second == ChessType::Black && !lastBlack_.valid()) lastBlack_ = it->first;
        if (it->second == ChessType::White && !lastWhite_.valid()) lastWhite_ = it->first;
        if (lastBlack_.valid() && lastWhite_.valid()) break;
    }
    status_ = "InProgress";
    thinking_ = false;
    message_ = "Resume loaded";

    storage_.setEnabled(true);
    storage_.startGame(bs, wl, playerNameOf(p1Type_), playerNameOf(p2Type_), p1Type_, p2Type_);
    for (const auto& h : history_) storage_.recordMove(h.first.r, h.first.c, h.second);
    publishLocked();
}

// 用 loop 私有状态刷新共享快照，并通知所有等待者（SSE / 请求线程）
void SessionController::publishLocked() {
    int n = board_.size();
    if (n <= 0) n = newBs_;

    snap_.status = status_;
    snap_.boardSize = n;
    snap_.winLength = (board_.size() > 0) ? board_.winLen() : newWl_;

    snap_.cells.assign(static_cast<size_t>(n) * n, 0);
    for (int r = 0; r < n && board_.size() > 0; r++)
        for (int c = 0; c < n; c++)
            snap_.cells[static_cast<size_t>(r) * n + c] = static_cast<int>(board_.at(r, c));

    snap_.turn = static_cast<int>(turn_);
    snap_.lastBlackR = lastBlack_.r; snap_.lastBlackC = lastBlack_.c;
    snap_.lastWhiteR = lastWhite_.r; snap_.lastWhiteC = lastWhite_.c;
    snap_.moveCount = static_cast<int>(history_.size());
    snap_.canUndo = !history_.empty() && status_ != "Idle";
    snap_.thinking = thinking_;
    snap_.humanTurn = (status_ == "InProgress") &&
                      ((turn_ == ChessType::Black ? p1Type_ : p2Type_) == 1);
    snap_.blackName = playerNameOf(p1Type_);
    snap_.whiteName = playerNameOf(p2Type_);
    snap_.message = message_;

    ++version_;
    cv_.notify_all();
}