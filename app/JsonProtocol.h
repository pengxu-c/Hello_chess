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

// ---- 从不可信 JSON 里取字段的唯一实现 ----
// nlohmann::json::value<T>() 在字段类型不匹配时会抛 type_error；命令分发
// 跑在界面线程上，异常穿透线程入口就是 std::terminate。所有 HTTP 入参
// （新局参数、命令字段、回放参数）都改走这里，缺字段/类型不对都回退默认值。
template <typename T>
T jsonValueOr(const nlohmann::json& j, const char* key, T fallback) {
    if (!j.is_object()) return fallback;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    try { return it->get<T>(); } catch (...) { return fallback; }
}

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
