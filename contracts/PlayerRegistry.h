// ============================================================
// contracts/PlayerRegistry.h - 玩家插件注册表 + 创建上下文
//
// 【扩展点 1：新增玩家】
//   1) 写一个类实现 gomoku::IPlayer（可以只写 tick 一个方法）
//   2) 在 plugins/PlayerRegistry.cpp 的 registerBuiltinPlayers() 里加一行 add()
//   3) 在 CMakeLists.txt 的源文件列表里加上你那个 .cpp
//   完事 —— 这个玩家会立刻出现在所有界面的下拉框里（Web / CLI 自动同步）。
//
// PlayerContext 是「玩家能拿到的全部环境」：只有规则、配置、棋盘尺寸。
// 拿不到界面、拿不到存储、拿不到 HTTP —— 玩家插件不可能污染别的层。
// ============================================================
#pragma once
#include "GameTypes.h"
#include "IPlayer.h"
#include "PluginRegistry.h"
#include <string>
#include <vector>

namespace gomoku {

class IRules;

// ---- 创建玩家时提供的上下文 ----
struct PlayerContext {
    const IRules* rules = nullptr;    // 需要自己推演的玩家（如搜索型）用得上
    int boardSize = 15;               // 当前棋盘尺寸（部分引擎要预分配）
    int winLength = 5;
    int thinkTimeMs = 1500;           // 默认思考预算（玩家可忽略）
};

class IPlayer;
using PlayerRegistry = PluginRegistry<IPlayer, PlayerInfo, PlayerContext>;

// 全局唯一注册表（首次访问即完成内置玩家注册）
PlayerRegistry& playerRegistry();

// 注册全部内置玩家插件 —— 唯一注册点，见 plugins/PlayerRegistry.cpp
void registerBuiltinPlayers(PlayerRegistry& reg);

// 供界面下拉框使用：全部玩家目录（含 ready / fallbackId，界面自行决定是否置灰）
std::vector<PlayerInfo> playerCatalog();

// 便捷：按 id 创建玩家并按 fallbackId 回退；第二个返回值表示实际使用的 id。
// 返回 nullptr 表示该 id 不可用且没有可用回退（调用方应报错而非静默继续）。
PlayerPtr createPlayer(const std::string& id,
                       const PlayerContext& ctx,
                       std::string* usedId = nullptr);

// 该玩家是否存在且可用（人类玩家也算「可用」）
bool playerAvailable(const std::string& id);

}  // namespace gomoku
