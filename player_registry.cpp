// ============================================================
// player_registry.cpp - 玩家注册表实现（内置棋手集中注册于此）
//
// 唯一注册点：新增一个棋手，只需在此文件的 playerRegistry() 里 add 一行。
// 其余所有入口（Web / CLI / selfplay / bench）一律通过注册表创建玩家。
// ============================================================
#include "player_registry.h"
#include "player.h"
#include "ai_player.h"
#include "tactical_max.h"
#include "ui.h"

#include <algorithm>

// ---------------- 注册表基本操作 ----------------

void PlayerRegistry::add(PlayerInfo info, PlayerFactory factory) {
    items_.emplace_back(std::move(info), std::move(factory));
}

const PlayerInfo* PlayerRegistry::find(int id) const {
    for (const auto& it : items_)
        if (it.first.id == id) return &it.first;
    return nullptr;
}

bool PlayerRegistry::contains(int id) const {
    return find(id) != nullptr;
}

const PlayerFactory* PlayerRegistry::factoryOf(int id) const {
    for (const auto& it : items_)
        if (it.first.id == id) return &it.second;
    return nullptr;
}

// 按编号创建。需要配置的档位（如 API AI）不可用时按 fallbackId 回退。
std::unique_ptr<Player> PlayerRegistry::create(int id, const PlayerContext& ctx) const {
    const PlayerInfo* info = find(id);
    if (!info) return nullptr;

    int rid = id;
    if (info->fallbackId != 0 && info->fallbackId != id &&
        ctx.aiConfig && !ctx.aiConfig->enabled) {
        rid = info->fallbackId;
    }

    const PlayerFactory* f = factoryOf(rid);
    if (!f || !(*f)) return nullptr;
    return (*f)(ctx);
}

// ---------------- 元信息查询 ----------------

std::string PlayerRegistry::idNameOf(int id) const {
    const PlayerInfo* p = find(id);
    return p ? p->idName : "Unknown";
}

std::string PlayerRegistry::displayOf(int id) const {
    const PlayerInfo* p = find(id);
    return p ? p->display : "Unknown";
}

bool PlayerRegistry::isHuman(int id) const {
    const PlayerInfo* p = find(id);
    return p && p->human;
}

bool PlayerRegistry::isPlayable(int id) const {
    const PlayerInfo* p = find(id);
    return p && p->playable;
}

int PlayerRegistry::fallbackOf(int id) const {
    const PlayerInfo* p = find(id);
    return p ? p->fallbackId : 0;
}

std::vector<int> PlayerRegistry::ids() const {
    std::vector<int> v;
    v.reserve(items_.size());
    for (const auto& it : items_) v.push_back(it.first.id);
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<PlayerInfo> PlayerRegistry::catalog() const {
    std::vector<PlayerInfo> v;
    v.reserve(items_.size());
    for (const auto& it : items_) v.push_back(it.first);
    std::sort(v.begin(), v.end(),
              [](const PlayerInfo& a, const PlayerInfo& b) { return a.id < b.id; });
    return v;
}

// ---------------- 全局单例 + 内置棋手注册 ----------------
//
// 采用"首次访问即注册"的惰性单例：任何入口都不必记得手动初始化。
// id 沿用 1..7，与旧存档的 player1Type / player2Type 完全兼容。
PlayerRegistry& playerRegistry() {
    static PlayerRegistry reg = [] {
        PlayerRegistry r;

        // 1. 人类：无 AI 实例，落子来自 UI 输入
        r.add({ 1, "Human", "Human", true, false, 0 },
              [](const PlayerContext& c) -> std::unique_ptr<Player> {
                  if (!c.ui) return nullptr;                 // 无头场景不构造人类
                  return std::make_unique<HumanPlayer>(*c.ui);
              });

        // 2. EasyJudge：随机 + 堵
        r.add({ 2, "EasyJudge", "EasyJudge", false, true, 0 },
              [](const PlayerContext&) {
                  return std::make_unique<EasyJudgeAI>();
              });

        // 3. PureGreed 1.0：常规评分纯防守
        r.add({ 3, "PureGreed-1.0", "PureGreed 1.0", false, true, 0 },
              [](const PlayerContext&) {
                  return std::make_unique<GreedyScoringAI>(0.0, "PureGreed 1.0");
              });

        // 4. PureGreed 1.1：常规评分攻防同权
        r.add({ 4, "PureGreed-1.1", "PureGreed 1.1", false, true, 0 },
              [](const PlayerContext&) {
                  return std::make_unique<GreedyScoringAI>(1.0, "PureGreed 1.1");
              });

        // 5. Minimax++：α-β 搜索
        r.add({ 5, "Minimax++", "Minimax++", false, true, 0 },
              [](const PlayerContext& c) -> std::unique_ptr<Player> {
                  if (!c.judge) return nullptr;
                  return std::make_unique<MinimaxPP>(*c.judge);
              });

        // 6. API AI：依赖 config.json；未配置时回退 5（Minimax++）
        //    bench 目标不链接 ai_player.cpp（会引入 curl 依赖），故编译期关闭该注册
#ifndef GOMOKU_NO_API_PLAYER
        r.add({ 6, "API AI", "API AI", false, false, 5 },
              [](const PlayerContext& c) -> std::unique_ptr<Player> {
                  if (!c.aiConfig) return nullptr;
                  return std::make_unique<APIPlayer>(*c.aiConfig);
              });
#endif

        // 7. TacticalMax：增量评估 + PVS + VCF/VCT（最强）
        r.add({ 7, "TacticalMax", "TacticalMax", false, true, 0 },
              [](const PlayerContext& c) -> std::unique_ptr<Player> {
                  if (!c.judge) return nullptr;
                  return std::make_unique<TacticalMax>(*c.judge);
              });

        return r;
    }();
    return reg;
}