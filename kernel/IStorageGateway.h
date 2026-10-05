// ============================================================
// kernel/IStorageGateway.h - 记忆存储边界（内核的第三个可替换点）
//
// 【扩展点 3：换存储】
//   想从本地文本文件换成 SQLite / Redis / 云端排行榜 / 内存假实现，
//   只需实现本接口并在 hosts 里注入 —— 内核编排、玩家插件、界面插件
//   全部不用动，也不用重新编译它们。
//
// 语义约定（内核依赖这些约定，实现者必须遵守）：
//   * enabled() == false 时，除 setEnabled 外的一切操作都必须安全无副作用，
//     undo() 返回 0、listResumes() 返回空、inGame() 返回 false。
//   * history() 返回「当前对局」的落子序列，是悔棋/重建的唯一事实来源。
//   * beginGame()/record()/finish() 由内核在控制线程串行调用，无需加锁。
//
// 【另有一条更小的捷径】一个实现都不想写时，可以用 makeNullStorageGateway()
//   —— 内核会正常工作，只是没有悔棋/回放/统计。
// ============================================================
#pragma once
#include "../contracts/GameTypes.h"
#include "IBoard.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gomoku {

class IStorageGateway {
public:
    virtual ~IStorageGateway() = default;

    // ---- 开关 ----
    virtual bool enabled() const = 0;
    virtual void setEnabled(bool on) = 0;

    // ---- 对局生命周期 ----
    virtual void beginGame(const RulesConfig& rules,
                           const Seat& black, const Seat& white) = 0;
    virtual void record(const Move& m) = 0;
    virtual void finish(const std::string& status) = 0;   // 传 gomoku::status::*
    virtual bool inGame() const = 0;

    // ---- 悔棋（由存储层直接改棋盘，保证棋盘与记录永远同源） ----
    virtual int undo(int steps, IBoard& board) = 0;
    virtual int moveCount() const = 0;
    virtual bool canUndo() const = 0;
    virtual const std::vector<Move>& history() const = 0;

    // ---- 残局 ----
    virtual bool saveResume(const std::string& note, const ViewState& snap) = 0;
    virtual bool loadResume(const std::string& id,
                            RulesConfig& rules,
                            std::vector<Move>& moves,
                            std::string& blackId,
                            std::string& whiteId) = 0;
    virtual std::vector<std::string> listResumes() const = 0;

    // ---- 目录查询（供界面展示列表；返回的是元信息，不含棋盘） ----
    virtual std::vector<StoredBrief> listGames() const = 0;
    virtual std::vector<StoredBrief> listResumeBriefs() const = 0;

    // ---- 回放：取某局的落子序列（不含棋盘，由内核重放） ----
    // upTo < 0 表示取全部。返回 false 表示该 id 不存在。
    virtual bool loadMoves(const std::string& id, std::vector<Move>& moves,
                           StoredBrief& brief, bool resume) const = 0;

    // ---- 全局统计（键值对，界面自己决定怎么显示） ----
    virtual std::vector<std::pair<std::string, long long>> globalStats() const = 0;
};

using StoragePtr = std::unique_ptr<IStorageGateway>;

// 工厂：由宿主/插件层提供实现并注入 MatchSession。
// root 为数据根目录（实现可忽略）。
using StorageFactory = std::unique_ptr<IStorageGateway> (*)(std::string root);

// 空实现：什么都不记，但接口语义完整。用于单元测试或"纯对局不要记录"模式。
StoragePtr makeNullStorageGateway();

}  // namespace gomoku
