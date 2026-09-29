// ============================================================
// session.h - Web 模式对局状态机
// 单一线程（loop 线程）独占 Board 与玩家对象，负责回合推进与 AI 思考；
// HTTP 线程只通过加锁读写「共享快照」并投递请求，避免数据竞争。
// 锁只在本层，核心逻辑（Board/Judge/Player/StorageManager）不加锁。
// ============================================================
#pragma once
#include "core.h"
#include "ai_config.h"
#include "storage.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <nlohmann/json.hpp>

class Player;

// 对局快照：loop 线程写、HTTP 线程读，均在锁保护下进行
struct SessionSnapshot {
    std::string status = "Idle";     // Idle/InProgress/BlackWin/WhiteWin/Draw
    int boardSize = 15;
    int winLength = 5;
    std::vector<int> cells;          // r*N+c：0空 / 1黑 / -1白
    int turn = 1;                    // 1黑 / -1白
    int lastBlackR = -1, lastBlackC = -1;
    int lastWhiteR = -1, lastWhiteC = -1;
    int moveCount = 0;
    bool canUndo = false;
    bool thinking = false;
    bool humanTurn = false;
    std::string blackName;
    std::string whiteName;
    std::string message;

    nlohmann::json toJson() const;   // 序列化为前端状态对象
};

class SessionController {
public:
    SessionController();
    ~SessionController();

    SessionController(const SessionController&) = delete;
    SessionController& operator=(const SessionController&) = delete;

    // ---- HTTP 请求入口（线程安全） ----
    void newGame(int boardSize, int winLength, int p1Type, int p2Type, bool storageEnabled);
    void humanMove(int r, int c);
    void undo(int n);
    void abort();                                  // 中止当前对局（保留棋盘，回到 Idle）
    void loadResume(const std::string& id);
    void requestQuit();

    SessionSnapshot snapshot() const;              // 取当前快照
    long long version() const;                     // 状态版本号
    bool waitForChange(long long& seen);           // 阻塞等版本变化；退出返回 false

    // ---- 存储 / 统计（线程安全） ----
    nlohmann::json listGames() const;
    nlohmann::json listResumes() const;
    nlohmann::json globalStats() const;
    bool saveResume(const std::string& note);
    nlohmann::json replay(const std::string& id, int step) const;

    nlohmann::json playerCatalog() const;          // 棋手目录（供前端下拉框，含 API 配置状态）
    void join();                                   // 等待 loop 线程结束

private:
    // ---- 共享状态（mtx_ 保护） ----
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    long long version_ = 0;
    SessionSnapshot snap_;
    std::atomic<bool> quit_{false};

    // 待处理请求（mtx_ 保护）
    bool reqNew_ = false;
    int newBs_ = 15, newWl_ = 5, newP1_ = 1, newP2_ = 2;
    bool newStorage_ = true;   // 存储默认开启（与 StorageConfig 默认值一致）
    bool reqMove_ = false;
    int moveR_ = -1, moveC_ = -1;
    int reqUndo_ = 0;
    bool reqAbort_ = false;
    bool reqLoad_ = false;
    std::string loadId_;

    // ---- loop 线程私有（仅 loop 线程访问，无需锁） ----
    std::thread loop_;
    Board board_;
    Judge judge_;
    AIConfig aiConfig_;
    StorageManager storage_;
    Player* p1_ = nullptr;                 // 人类(类型1)时为 nullptr
    Player* p2_ = nullptr;
    int p1Type_ = 1, p2Type_ = 1;
    ChessType turn_ = ChessType::Black;
    std::string status_ = "Idle";
    Pos lastBlack_{ -1, -1 }, lastWhite_{ -1, -1 };
    bool thinking_ = false;
    std::string message_;
    // 落子历史不再在此维护：StorageManager 是唯一事实来源（悔棋/重建/步数均由其提供）

    void runLoop();                        // loop 线程主体
    void publishLocked();                  // 用 loop 私有状态刷新 snap_ 并通知
    void applyMoveLocked(int r, int c);    // 处理一步落子（胜负/回合/记录）
    void undoLocked(int n);                // 悔棋（重建棋盘）
    void abortLocked();                    // 中止对局（结束存储记录，状态回 Idle）
    void startNewGameLocked();             // 按 newXxx_ 参数开新局
    void loadResumeLocked(const std::string& id);
    Player* createPlayer(int choice);      // 人类(1) 返回 nullptr
    void releasePlayers();
};