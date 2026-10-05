// ============================================================
// kernel/MatchSession.h - 对局会话：把内核接上玩家插件、存储、观察者
//
// 【它在微内核里的位置】
//   契约层 IControllable 的官方实现。界面插件只认接口，不认本类；
//   本类只认 IPlayer 接口，不认任何具体玩家。
//   两侧都是插件，中间的编排逻辑（谁该走、AI 在工作线程思考、悔棋、换人）
//   集中在这里 —— 这是内核里唯一「有流程」的地方，其余内核部件都是纯逻辑。
//
// 【线程模型】（面试常问的那种，这里写清楚）
//   * 控制线程：唯一可以改棋盘的线程。所有命令都在它上面串行执行。
//   * 调用方线程（HTTP / 界面）：只做两件事 —— 投递命令、读只读快照。
//   * AI 思考：AI 玩家自己在后台线程里跑（IPlayer::async 为 true 时），
//     思考期间控制线程不持锁，界面照样能刷新、能悔棋、能换人；
//     若思考期间来了新命令，本次 AI 结果作废（用请求代数 reqGen_ 检测）。
//   * 观察者回调：在锁外调用，界面插件怎么慢都不会互相拖死。
// ============================================================
#pragma once
#include "../contracts/IControllable.h"
#include "../contracts/IPlayer.h"
#include "../contracts/PlayerRegistry.h"
#include "../contracts/ViewRegistry.h"
#include "GomokuRules.h"
#include "IStorageGateway.h"
#include "MatchState.h"
#include "SquareBoard.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gomoku {

class IView;

// 玩家工厂：默认走全局注册表；宿主可注入自己的工厂（测试替身 / 脚本化对局）。
using PlayerFactory = std::function<PlayerPtr(const std::string& id, const PlayerContext&)>;

class MatchSession final : public IControllable {
public:
    // storage ：记忆存储实现；传 nullptr 则退化为"不记录"的空存储。
    //          由宿主决定用哪种存储 —— 内核不认识具体实现。
    // factory ：为空则使用全局 playerRegistry()
    explicit MatchSession(StoragePtr storage, PlayerFactory factory = {});
    ~MatchSession() override;

    MatchSession(const MatchSession&) = delete;
    MatchSession& operator=(const MatchSession&) = delete;

    // ---- 生命周期 ----
    void start();                     // 启动控制线程
    void join(int timeoutMs = 50) override;   // 等待控制线程退出（带超时，界面主循环用）
    void requestQuit() override;
    bool quitting() const override { return quit_.load(); }
    bool finished() const { return finished_.load(); }

    // ---- IControllable ----
    bool startGame(const RulesConfig& rules,
                   const std::string& blackPlayerId,
                   const std::string& whitePlayerId,
                   bool storageEnabled = true) override;
    bool play(int r, int c) override;
    bool undo(int steps = 1) override;
    bool abort() override;
    bool swapPlayer(Stone seat, const std::string& playerId) override;
    bool saveResume(const std::string& note = "") override;
    bool loadResume(const std::string& id) override;
    bool setStorageEnabled(bool enabled) override;
    std::vector<StoredBrief> listGames() const override;
    std::vector<StoredBrief> listResumes() const override;
    ReplayFrame replay(const std::string& id, int step) const override;
    std::vector<std::pair<std::string, long long>> stats() const override;
    void addObserver(IView* view) override;
    void removeObserver(IView* view) override;

    // ---- 只读快照（线程安全，供界面/宿主/HTTP 读取） ----
    ViewState state() const;
    long long version() const;

    // ---- 阻塞等待状态变化（SSE 推送用）----
    // 最多等待 timeoutMs；返回 false 表示会话已退出。
    bool waitForChange(long long& seenVersion, int timeoutMs = 15000);

    // ---- 思考预算（宿主可调；自对弈可设很大） ----
    void setThinkBudgetMs(int ms) { thinkBudgetMs_ = ms; }
    int  thinkBudgetMs() const { return thinkBudgetMs_; }

    // ---- 存储开关默认值（开新局未显式指定时使用） ----
    void setStorageEnabledDefault(bool on) { storageDefault_ = on; }

private:
    // ================= 控制线程 =================
    void controlLoop();
    bool hasWorkLocked() const;
    bool handleAiTurn();                       // 返回 true 表示还要继续循环

    // ---- 待处理请求（由调用方线程写入，控制线程消费） ----
    struct Pending {
        bool reqNew = false;
        RulesConfig newRules;
        std::string newBlack, newWhite;
        bool reqStorage = false;
        bool newStorage = true;

        bool reqPlay = false;
        int  playR = -1, playC = -1;

        int  reqUndo = 0;

        bool reqAbort = false;

        bool reqSwap = false;
        Stone swapSeat = Stone::Black;
        std::string swapId;

        bool reqSave = false;
        std::string saveNote;

        bool reqLoad = false;
        std::string loadId;

        bool reqStorageToggle = false;
        bool storageToggleValue = false;

        bool any() const {
            return reqNew || reqPlay || reqUndo > 0 || reqAbort || reqSwap ||
                   reqSave || reqLoad || reqStorageToggle;
        }
        void clear() { *this = Pending{}; }
    };

    // ---- 命令处理（均要求持有 stateMtx_） ----
    void doStartGameLocked(const Pending& req);
    void doPlayLocked(int r, int c);
    void doUndoLocked(int steps);
    void doAbortLocked();
    void doSwapLocked(Stone seat, const std::string& id);
    void doSaveLocked(const std::string& note);
    void doLoadLocked(const std::string& id);
    void doStorageToggleLocked(bool on);

    // ---- 观察者 ----
    //
    // publishLocked() 只在锁内「攒一帧」，真正的界面回调由控制线程在锁外
    // 派发（见 dispatchNotifications）。这样做的原因见 MatchSession.cpp 里的
    // 锁链分析：界面 render() 会去拿界面自己的锁，若内核持着 stateMtx_ 去回调，
    // 一次慢的网络写就能把整局对局冻住。
    void publishLocked();
    void dispatchNotifications();                 // 锁外调用界面
    ViewState snapshotLocked() const;

    // ---- 玩家 ----
    PlayerPtr makePlayer(const std::string& id, std::string* usedId);
    void setSeatsLocked(const std::string& blackId, const std::string& whiteId);
    void releasePlayersLocked();
    IPlayer* playerOnTurnLocked() const;

    void setMessageLocked(const std::string& msg);
    static std::string statusFromMatch(const MatchState& m);

    // ================= 状态 =================
    // 注意成员初始化顺序 = 声明顺序：构造函数体里会用到这些配置项，
    // 因此它们必须声明在 board_/match_/store_ 之前（曾因顺序写反，
    // 导致 storageDefault_ 未初始化就被读取，悔棋静默失效）。
    int thinkBudgetMs_ = 1500;
    bool storageDefault_ = true;

    SquareBoard board_;
    GomokuRules rules_;
    MatchState match_;
    StoragePtr store_;

    PlayerPtr black_;
    PlayerPtr white_;
    Seat blackSeat_;
    Seat whiteSeat_;

    std::string status_ = status::kIdle;
    std::string message_;
    std::string lastNotifiedMessage_;   // 只在消息真正变化时打扰观察者
    bool thinking_ = false;
    long long version_ = 0;

    // 同步
    mutable std::mutex stateMtx_;
    std::condition_variable stateCv_;    // 状态变化（SSE/等待者）
    std::condition_variable workCv_;     // 控制线程等待工作
    Pending pending_;
    int reqGen_ = 0;                     // 请求代数：检测"AI 思考期间来了新命令"
    int handledGen_ = 0;
    bool humanAwaiting_ = false;         // 人类回合，等待 play()
    std::atomic<bool> quit_{ false };
    std::atomic<bool> finished_{ false };
    std::thread controlThread_;

    std::vector<IView*> observers_;

    // 待派发的状态帧（publishLocked 在锁内填充，dispatchNotifications 在锁外消费）。
    // 只保留最新一帧：界面要的是"当前状态"，中间帧没有意义，
    // 合并天然防止了高频落子时通知队列无限增长。
    struct PendingFrame {
        ViewState snap;
        std::vector<IView*> targets;
        bool hasNotify = false;
        bool valid = false;
    };
    PendingFrame pendingFrame_;

    PlayerFactory playerFactory_;
};

}  // namespace gomoku
