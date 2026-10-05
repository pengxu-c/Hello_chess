// ============================================================
// plugins/PlayerRegistry.cpp - 玩家插件注册：全项目唯一的注册点
// ============================================================
//
//  ██  新增一个玩家，只需要动这个文件一处  ██
//
//  步骤：
//    1) 写一个类实现 gomoku::IPlayer（参考 plugins/players/NativePlayers.h，
//       最短只要十几行）
//    2) 在下面 registerBuiltinPlayers() 里加一行 reg.add(...)
//    3) 把新 .cpp 加进 CMakeLists.txt 的 GOMOKU_CORE_SOURCES
//
//  加完之后：Web 界面下拉框、CLI 选人菜单、自对弈、bench 全部自动出现该玩家，
//  因为所有人都只读这一张注册表 —— 这就是微内核要买到的东西。
//
#include "../contracts/PlayerRegistry.h"
#include "players/EnginePlayers.h"
#include "players/NativePlayers.h"

#include <algorithm>
#include <memory>

#ifndef GOMOKU_NO_API_PLAYER
// API 玩家是唯一带网络依赖（libcurl）的插件。bench 不链接网络栈，
// 因此在编译期就把这一整块排除掉（见 CMakeLists 的 GOMOKU_NO_API_PLAYER）。
#include "../app/ResourcePaths.h"   // executableDir：定位 config.json
#include "../engine/ai_config.h"     // AIConfig
#include "../engine/ai_player.h"     // APIPlayer
#include "../engine/core.h"          // 旧棋盘（把契约快照翻译给 APIPlayer）
#endif

namespace gomoku {
namespace {

// 人类玩家的占位实现。
// 它永远不会被 tick()：IPlayer::human() 返回 true，内核就不会让 AI 线程碰它，
// 而是把落子权交给界面（界面通过 IControllable::play(r,c) 送回坐标）。
// 这样「人类输入」与「AI 思考」在架构上彻底分家 —— 旧代码把它们混在
// Player::place() 里轮询，是界面/玩家耦合的根源。
class HumanPlayerStub final : public IPlayer {
public:
    std::string id() const override { return "human"; }
    std::string displayName() const override { return "Human"; }
    bool human() const override { return true; }
    bool async() const override { return false; }
    Decision tick(const ViewState&, const ThinkBudget&) override {
        return Decision::pass("human input comes from the view, not from tick()");
    }
};

// ============================================================
// API AI：唯一带外部依赖（网络 + API Key）的玩家
//
// 刻意单独放在本文件而不是 EnginePlayers.cpp：
//   * bench 只跑本地算法，不链接 libcurl → 网络依赖被关在这里；
//   * 未配置时 create() 返回 nullptr → EnginePlayer::available() 为 false，
//     界面自动置灰、开局自动回退，不需要任何额外约定。
// ============================================================
#ifndef GOMOKU_NO_API_PLAYER
class ApiEngineHandle final : public IEngineHandle {
public:
    explicit ApiEngineHandle(AIConfig cfg) : cfg_(std::move(cfg)) {}

    std::string name() const override { return "API AI"; }

    Coord decide(int boardSize, int winLength, const std::vector<Move>& history,
                 Stone color) override {
        if (board_.size() != boardSize) {
            board_.resize(boardSize);
            builtMoves_ = -1;
        }
        board_.setWinLen(winLength);
        if (builtMoves_ != static_cast<int>(history.size())) {
            board_.clear();
            for (const auto& m : history)
                board_.place(m.r, m.c, static_cast<ChessType>(static_cast<int>(m.color)));
            builtMoves_ = static_cast<int>(history.size());
        }

        if (!api_) api_ = std::make_unique<APIPlayer>(cfg_);
        const Pos p = api_->place(board_, static_cast<ChessType>(static_cast<int>(color)));
        builtMoves_ = -1;   // APIPlayer 不保证不动棋盘，下一步重建
        if (!p.valid()) return Coord{ -1, -1 };
        return Coord{ p.r, p.c };
    }

private:
    AIConfig cfg_;
    std::unique_ptr<APIPlayer> api_;
    Board board_;
    int builtMoves_ = -1;
};

EngineBinding apiEngineBinding() {
    return EngineBinding{
        "api-ai", "API AI", "远程大模型（需 config.json）",
        []() -> std::shared_ptr<IEngineHandle> {
            AIConfig cfg;
            // 【为什么要逐个候选路径试】
            //   AIConfig 原本只认相对路径 "config.json"，也就是"当前工作目录下的"。
            //   双击 exe 运行时 CWD 是随机的（可能是桌面、可能是别的盘），
            //   于是永远读不到配置文件 → API 玩家永远是灰的，
            //   而日志只说"config.json not found"，看不出到底在找哪。
            //   这里按"exe 同级 → exe 上溯两级 → 当前目录"逐个试，
            //   与 ResourcePaths 的定位策略一致。
            const std::string exe = executableDir();
            const std::string candidates[] = {
                exe + "/config.json",
                exe + "/../../config.json",
                "config.json",
            };
            bool loaded = false;
            for (const std::string& path : candidates) {
                if (cfg.loadFromFile(path)) { loaded = true; break; }
            }
            if (!loaded || !cfg.enabled) return nullptr;
            return std::make_shared<ApiEngineHandle>(cfg);
        },
        {} };
}
#endif  // GOMOKU_NO_API_PLAYER

}  // namespace

// ============================================================
// 内置玩家注册
// ============================================================
void registerBuiltinPlayers(PlayerRegistry& reg) {
    // ---- 1. 人类（落子来自界面输入） ----
    reg.add(PlayerInfo{ "human", "Human", "由界面点击落子", true, true, "" },
            [](const PlayerContext&) -> PlayerPtr {
                return std::make_unique<HumanPlayerStub>();
            });

    // ---- 2. 原生示例玩家：证明"不需要框架也能写插件" ----
    reg.add(PlayerInfo{ "random", "Random", "均匀随机（最弱陪练）", false, true, "" },
            [](const PlayerContext&) -> PlayerPtr {
                return std::make_unique<RandomPlayer>();
            });

    reg.add(PlayerInfo{ "greedy", "Greedy", "一步贪心：延己线 + 堵对方", false, true, "" },
            [](const PlayerContext&) -> PlayerPtr {
                return std::make_unique<GreedyPlayer>();
            });

    // ---- 3. 既有 AI 引擎：以适配器形式批量接入 ----
    // 引擎清单在 plugins/players/EnginePlayers.cpp 的 builtinEngineBindings()，
    // 这里只负责把每个绑定翻译成一条注册项（依然是"一行一个引擎"）。
    auto addBinding = [&reg](const EngineBinding& binding) {
        EngineBinding b = binding;   // 拷贝：lambda 需要按值捕获
        PlayerInfo info;
        info.id = b.id;
        info.display = b.display;
        info.description = b.description;
        info.human = false;
        info.ready = true;

        // 需要外部配置的引擎声明回退目标（API AI → Minimax++）
        if (b.id == "api-ai") info.fallbackId = "minimax";

        reg.add(info, [b](const PlayerContext&) -> PlayerPtr {
            return std::make_unique<EnginePlayer>(b);
        });
    };

    for (const auto& binding : builtinEngineBindings()) addBinding(binding);

#ifndef GOMOKU_NO_API_PLAYER
    // ---- 4. 带外部依赖的玩家（网络 / 未来可能的模型文件） ----
    addBinding(apiEngineBinding());
#endif
}

// ============================================================
// 全局注册表（惰性单例：任何入口首次访问即完成注册，无需手动初始化）
// ============================================================
PlayerRegistry& playerRegistry() {
    static PlayerRegistry reg = [] {
        PlayerRegistry r;
        registerBuiltinPlayers(r);
        return r;
    }();
    return reg;
}

std::vector<PlayerInfo> playerCatalog() {
    // 目录按值返回，调用方可任意排序/过滤，不影响注册表。
    // ready 字段在这里补齐：需要外部配置的插件才可能为 false。
    std::vector<PlayerInfo> out = playerRegistry().catalog();
    for (auto& info : out) info.ready = playerAvailable(info.id);
    return out;
}

PlayerPtr createPlayer(const std::string& id, const PlayerContext& ctx, std::string* usedId) {
    PlayerRegistry& reg = playerRegistry();

    auto tryCreate = [&](const std::string& want) -> PlayerPtr {
        if (!reg.contains(want)) return nullptr;
        return reg.create(want, ctx);
    };

    if (PlayerPtr p = tryCreate(id)) {
        if (usedId) *usedId = id;
        return p;
    }

    // 主选择不可用（如 API AI 未配置）→ 按注册的 fallbackId 回退
    const PlayerInfo* info = reg.find(id);
    if (info && !info->fallbackId.empty()) {
        if (PlayerPtr p = tryCreate(info->fallbackId)) {
            if (usedId) *usedId = info->fallbackId;
            return p;
        }
    }
    if (usedId) usedId->clear();
    return nullptr;
}

bool playerAvailable(const std::string& id) {
    PlayerRegistry& reg = playerRegistry();
    if (!reg.contains(id)) return false;
    // 只有「声明了回退」的插件才需要外部配置（目前是 API AI：它要去读 config.json）。
    // 其余的插件没有外部依赖，构造出来就一定可用。
    const PlayerInfo* info = reg.find(id);
    if (!info || info->fallbackId.empty()) return true;

    // 实测一次：能造出真正的引擎才算可用（API AI 此时会去读 config.json）
    std::unique_ptr<IPlayer> probe = reg.create(id, PlayerContext{});
    return probe && probe->available();
}

}  // namespace gomoku
