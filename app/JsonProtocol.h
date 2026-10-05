// ============================================================
// app/JsonProtocol.h - 内核状态 ↔ JSON 的唯一转换点
//
// 「网页界面」和「HTTP 宿主」都不直接碰内核结构体：它们只认 JSON。
// 于是：
//   * 内核字段改名 → 只改这一个文件；
//   * 加一个新界面（比如 Electron / 微信小程序）→ 复用同一份 JSON；
//   * 协议可以单独做版本兼容，不影响内核语义。
//
// 玩家目录 / 消息 / 状态快照三类数据的形状都定义在这里。
// ============================================================
#pragma once
#include "../contracts/GameTypes.h"
#include "../contracts/PlayerRegistry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace gomoku {

// ---- 状态快照 → JSON（前端渲染用） ----
nlohmann::json toJson(const ViewState& s);

// ---- 玩家目录 → JSON（界面下拉框用；含 ready 标记用于置灰） ----
nlohmann::json playerCatalogJson(const std::vector<PlayerInfo>& catalog);

// ---- JSON → 开新局参数（缺字段时给安全默认值） ----
struct NewGameRequest {
    RulesConfig rules;
    std::string black = "human";
    std::string white = "human";
    bool storageEnabled = true;
};
NewGameRequest parseNewGame(const nlohmann::json& body);

// ---- 统一响应体：{ ok, error? , message? } ----
nlohmann::json okJson();
nlohmann::json errJson(const std::string& what);

// 已持久化的棋局 / 残局的目录项 → JSON
nlohmann::json storedListJson(const std::vector<StoredBrief>& items);

}  // namespace gomoku
