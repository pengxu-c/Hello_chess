// ============================================================
// plugins/storage/LegacyStorageGateway.h - 把既有 StorageManager 接成存储插件
//
// 旧存储（data/*.txt 文本格式、悔棋、残局、统计）已经在用且兼容旧存档，
// 因此微内核不重写它，只包一层适配 —— 和引擎适配器同样的思路。
//
// 想换成 SQLite / 云存档：新写一个 IStorageGateway 实现，在宿主里换掉工厂函数，
// 内核、界面、玩家全都不用改。这就是「存储也是插件」的含义。
// ============================================================
#pragma once
#include "../../kernel/IStorageGateway.h"
#include <memory>
#include <string>

namespace gomoku {

// 造一个基于旧 StorageManager 的存储网关。
// root 为数据根目录（默认 "data"）；带 .txt 的旧存档可直接沿用。
StoragePtr makeLegacyStorageGateway(const std::string& root);

// 与 StorageFactory 签名一致的工厂函数，供宿主直接注入 MatchSession。
StoragePtr legacyStorageFactory(std::string root);

}  // namespace gomoku
