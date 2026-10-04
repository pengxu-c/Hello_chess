// ============================================================
// player_registry.h - 玩家注册表（微内核插件化的关键一环）
//
// 动机：原项目在 controller / session / main(selfplay) / bench 四处
// 各写了一份 "编号 → new 具体棋手" 的 switch，新增一个玩家要改 4 处。
// 本注册表把「创建玩家」收敛为唯一入口：
//     新增玩家 = 写 1 个类 + 在 player_registry.cpp 加 1 行注册。
//
// 统一构造上下文 PlayerContext：不同棋手需要的环境不同
//     （MinimaxPP/TacticalMax 要 Judge&，Human 要 UI&，APIPlayer 要 AIConfig），
// 用同一个结构体打包传入，从而消除各不相同的工厂签名。
//
// 通过 playerRegistry() 惰性获取全局单例：首次访问即完成内置棋手注册，
// 任何入口（主程序 / bench / 未来插件）都无需记得手动初始化。
// ============================================================
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Player;
class Judge;
class IUi;
struct AIConfig;

// ---- 创建玩家所需的上下文（谁需要什么就取什么） ----
struct PlayerContext {
    Judge*          judge    = nullptr;   // MinimaxPP / TacticalMax 需要
    IUi*            ui       = nullptr;   // HumanPlayer 需要（无头场景传 nullptr）
    const AIConfig* aiConfig = nullptr;   // APIPlayer 需要
};

// ---- 工厂：给定上下文，产出一个玩家对象 ----
using PlayerFactory = std::function<std::unique_ptr<Player>(const PlayerContext&)>;

// ---- 玩家元信息（供创建、显示、目录、bench 过滤） ----
struct PlayerInfo {
    int         id = 0;          // 编号（1..7，与旧存档一致）
    std::string idName;          // 内部/日志短名，如 "PureGreed-1.0"
    std::string display;         // 界面显示名，如 "PureGreed 1.0"
    bool        human = false;   // 是否为人类（无 AI 实例，落子来自输入）
    bool        playable = false;// 是否可用于自动对弈 / bench
    int         fallbackId = 0;  // 不可用时的回退编号（0 表示不涉及；API=6 回退 5）
};

// ---- 玩家注册表：编号 → 元信息 + 工厂 ----
class PlayerRegistry {
public:
    void add(PlayerInfo info, PlayerFactory factory);

    // 按编号创建玩家；API 未配置等情况按 fallbackId 自动回退。
    // 人类编号或上下文不足时可能返回 nullptr（与旧行为一致）。
    std::unique_ptr<Player> create(int id, const PlayerContext& ctx) const;

    const PlayerInfo* find(int id) const;
    bool contains(int id) const;

    std::string idNameOf(int id) const;      // 未知编号返回 "Unknown"
    std::string displayOf(int id) const;     // 未知编号返回 "Unknown"
    bool isHuman(int id) const;
    bool isPlayable(int id) const;
    int  fallbackOf(int id) const;           // 0 表示自身

    std::vector<int> ids() const;            // 已注册编号（升序）
    std::vector<PlayerInfo> catalog() const; // 全部元信息（升序）

private:
    const PlayerFactory* factoryOf(int id) const;
    std::vector<std::pair<PlayerInfo, PlayerFactory>> items_;
};

// 全局唯一实例（惰性注册内置棋手）
PlayerRegistry& playerRegistry();