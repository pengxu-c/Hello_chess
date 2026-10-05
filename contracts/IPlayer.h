// ============================================================
// contracts/IPlayer.h - 玩家插件契约（微内核的「玩家」边界）
//
// 【这是本项目最重要的一个接口】
// 一个玩家 = 实现本接口的一个类 + 在 plugins/PlayerRegistry.cpp 注册一行。
// 不需要改内核、不需要改界面、不需要改宿主、不需要改 CMake。
//
// 设计要点（与旧代码的关键区别）：
//   1. decide() 是「同步思考」，由内核在后台工作线程中调用 —— 因此远程 API、
//      深度学习推理这类耗时玩家不会卡住界面；旧代码把人类输入和 AI 思考混在
//      同一个 place() 轮询里，是界面与玩家耦合的根源。
//   2. 人类玩家不再需要轮询界面：直接实现 human()=true，内核会把落子权利
//      交给界面，通过 IControllable::play() 送达（见 contracts/IControllable.h）。
//   3. 玩家看不到棋盘实现、看不到界面、看不到网络 —— 只看到只读快照与落子历史。
//      想换棋盘（方形/六边形/立体）只需换内核，玩家插件一行都不用改。
// ============================================================
#pragma once
#include "GameTypes.h"
#include <memory>
#include <string>

namespace gomoku {

// ---- 思考预算：内核告知「你有多少时间」，玩家自行取舍 ----
struct ThinkBudget {
    int timeBudgetMs = 1500;   // 期望思考时间上限（AI 引擎据此分配搜索深度）
    bool unlimited = false;    // 无限制（自对弈/离线跑谱场景）
};

// ---- 决策结果：把「落子」与「无法落子」区分开，避免用无效坐标表达失败 ----
struct Decision {
    bool ok = false;           // 是否成功给出着法
    Coord move{ -1, -1 };      // ok 为真时有效
    std::string reason;        // 失败原因（超时/网络错误/无合法着法）

    static Decision pass(const std::string& why) { return Decision{ false, kNoCoord, why }; }
    static Decision at(int r, int c) { return Decision{ true, Coord{ r, c }, "" }; }
};

// ============================================================
// IPlayer - 玩家插件必须实现的全部内容
// ============================================================
class IPlayer {
public:
    virtual ~IPlayer() = default;

    // ---- 元信息 ----
    virtual std::string id() const = 0;            // 稳定标识（存档/接口用）
    virtual std::string displayName() const = 0;   // 界面显示名

    // 是否人类。返回 true 时内核不会调用 tick()，而是把落子权交给界面：
    // 界面通过 IControllable::play(r,c) 把坐标送进内核。
    virtual bool human() const { return false; }

    // 是否已在后台线程思考。
    //   true（默认）→ 内核在工作线程调用 tick()，界面保持流畅；
    //   false       → 内核在本线程调用 tick()（纯本地且极快时用）。
    virtual bool async() const { return true; }

    // 插件是否「已正确配置、可以工作」。
    // 默认 true；需要外部资源（API Key、模型文件、GPU）的玩家请覆盖它。
    // 界面据此把不可用档位置灰，注册表据此决定是否回退到 fallbackId。
    virtual bool available() const { return true; }

    // 每局开始/结束的回调（纯可选）：用于重置置换表、记录统计等。
    // 默认空实现 —— 玩家插件可以完全不关心对局生命周期。
    virtual void onMatchStart(const ViewState&) {}
    virtual void onMatchEnd(const ViewState&) {}

    // ---- 核心：给出下一步 ----
    // state 是只读快照；玩家如需内部可变棋盘，请自建（见 plugins/PlayerRegistry.cpp
    // 的 AdapterPlayer 如何把契约快照转成旧引擎需要的可变棋盘）。
    // 返回 Decision::pass(...) 表示放弃着法（内核判该方负）。
    virtual Decision tick(const ViewState& state, const ThinkBudget& budget) = 0;
};

using PlayerPtr = std::unique_ptr<IPlayer>;

}  // namespace gomoku
