// ============================================================
// contracts/IControllable.h - 界面插件「操作内核」的唯一通道
//
// 界面插件通过这个接口下命令，内核把命令排进队列、在控制线程里执行。
// 界面永远不直接碰棋盘 —— 这就是「前端界面与棋盘维护泾渭分明」的落点。
//
// 全部方法都是线程安全的、非阻塞的（投递即返回）。
// 返回值只表示「命令是否被接受」，最终结果通过 ViewState 快照回传。
// ============================================================
#pragma once
#include "GameTypes.h"
#include <string>
#include <utility>
#include <vector>

namespace gomoku {

// 一次回放的查询结果：某个棋局在指定步数时的棋盘样子。
struct ReplayFrame {
    bool ok = false;
    int boardSize = 15;
    int winLength = 5;
    std::vector<int> cells;        // r*boardSize+c
    int step = 0;                  // 当前展示到第几步
    int total = 0;                 // 总步数
    Coord last{ -1, -1 };          // 该步落子位置
    Stone lastColor = Stone::Empty;
    std::string black, white;
};

class IControllable {
public:
    virtual ~IControllable() = default;

    // ---- 对局生命周期 ----
    // 开新局：规则 + 双方玩家 id（来自 IPlayerRegistry 目录）+ 是否启用记忆存储
    virtual bool startGame(const RulesConfig& rules,
                           const std::string& blackPlayerId,
                           const std::string& whitePlayerId,
                           bool storageEnabled = true) = 0;

    // ---- 对局内操作 ----
    virtual bool play(int r, int c) = 0;                    // 人类落子（仅人类回合生效）
    virtual bool undo(int steps = 1) = 0;                   // 悔棋
    virtual bool abort() = 0;                               // 中止当前对局
    virtual bool swapPlayer(Stone seat, const std::string& playerId) = 0;  // 局中热替换玩家

    // ---- 记忆存储（关闭时这些命令一律无效，内核统一守卫） ----
    virtual bool saveResume(const std::string& note = "") = 0;
    virtual bool loadResume(const std::string& id) = 0;
    virtual bool setStorageEnabled(bool enabled) = 0;

    // ---- 查询（同步、只读、线程安全；界面拉列表/回放帧用） ----
    // 之所以放在这里而不是让界面直接持有存储：界面插件不应该认识存储实现。
    virtual std::vector<StoredBrief> listGames() const = 0;
    virtual std::vector<StoredBrief> listResumes() const = 0;
    virtual ReplayFrame replay(const std::string& id, int step) const = 0;
    virtual std::vector<std::pair<std::string, long long>> stats() const = 0;

    // ---- 观察者注册：界面插件挂上来后即可收到状态推送 ----
    // 注册时内核会立即同步一次当前状态，界面无需自己做首帧拉取。
    virtual void addObserver(class IView* view) = 0;
    virtual void removeObserver(class IView* view) = 0;

    // ---- 让出执行权一小会儿（界面主循环用，避免空转吃满 CPU） ----
    virtual void join(int timeoutMs = 50) = 0;

    // ---- 会话是否已进入退出流程（界面/驱动据此收尾） ----
    virtual bool quitting() const = 0;

    // ---- 退出整个进程（Web 界面的 Quit 按钮 / CLI 的 q 命令） ----
    virtual void requestQuit() = 0;
};

}  // namespace gomoku
