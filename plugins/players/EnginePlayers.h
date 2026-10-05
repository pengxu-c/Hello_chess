// ============================================================
// plugins/players/EnginePlayers.h - 把既有 AI 引擎接入微内核的适配器
//
// 【为什么不重写引擎】
//   tactical_max.cpp / player.cpp 是本项目最值钱的资产（棋力本身）。
//   微内核的目标是「谁都能换」，而不是「什么都重写」—— 因此这里用适配器
//   把旧引擎包成标准插件，引擎源码一行都不改。
//
// 【本文件刻意不依赖网络】
//   这里只提供「本地引擎」的统一外壳。需要外部资源（API Key / 模型文件 /
//   GPU）的玩家请写自己的 IEngineHandle 实现并单独注册 —— 见
//   plugins/PlayerRegistry.cpp 里的 API AI（它把 curl 依赖关在了插件内部，
//   所以 bench 目标可以完全不链接网络栈）。
//
// 【适配器顺手解决了旧架构的三个硬伤】
//   1. 旧接口 Player::place(Board&, ChessType) 需要一个可变的旧棋盘，
//      而契约层只给只读快照 → 适配器内部持有缓存棋盘，按落子历史重建。
//   2. 旧引擎的思考时间只能通过具体类型设置 → 适配器统一尝试 setTimeBudgetMs，
//      把「预算」语义收敛到契约层的 ThinkBudget。
//   3. 旧引擎靠 Judge& 判胜负 → 适配器自持一个裁判，不需要外部注入。
// ============================================================
#pragma once
#include "../../contracts/IPlayer.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gomoku {

// 旧引擎的统一外壳：把「历史 + 颜色」翻译成该引擎认识的形式，返回着法。
class IEngineHandle {
public:
    virtual ~IEngineHandle() = default;
    virtual std::string name() const = 0;
    // 让引擎针对 color 思考下一步；history 按落子先后排列。返回 {-1,-1} 表示放弃。
    virtual Coord decide(int boardSize, int winLength, const std::vector<Move>& history,
                         Stone color) = 0;
};

// 引擎描述：怎么造、怎么设预算。
struct EngineBinding {
    std::string id;              // 插件 id，如 "tactical-max"
    std::string display;         // 界面显示名
    std::string description;

    std::function<std::shared_ptr<IEngineHandle>()> create;   // 造一个引擎实例
    std::function<void(IEngineHandle*, int ms)> setBudget;    // 可选：设思考预算
};

// 把 EngineBinding 包成标准 IPlayer 插件。
class EnginePlayer final : public IPlayer {
public:
    explicit EnginePlayer(EngineBinding binding);
    ~EnginePlayer() override;

    std::string id() const override { return binding_.id; }
    std::string displayName() const override { return binding_.display; }
    bool async() const override { return true; }   // 重引擎一律放后台线程
    bool available() const override { return engine_ != nullptr; }

    void onMatchStart(const ViewState& state) override;
    Decision tick(const ViewState& state, const ThinkBudget& budget) override;

private:
    EngineBinding binding_;
    std::shared_ptr<IEngineHandle> engine_;
};

// 内置本地引擎绑定表（实现见 EnginePlayers.cpp）。
// 拆成独立函数是为了让「新增引擎」只需要改那个表一处。
std::vector<EngineBinding> builtinEngineBindings();

}  // namespace gomoku

