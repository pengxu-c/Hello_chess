// ============================================================
// plugins/storage/LegacyStorageGateway.cpp - 适配实现
//
// 旧 StorageManager 已经是「唯一事实来源」（内部维护落子序列并直接操作棋盘），
// 因此这里基本是转发；少数不匹配的地方（状态枚举、玩家类型编号 ↔ 插件 id）
// 在下面显式转换，转换规则集中在本文件，不外泄。
// ============================================================
#include "LegacyStorageGateway.h"

#include "../../engine/storage.h"              // legacy::StorageManager
#include "../../contracts/PlayerRegistry.h"

#include <utility>

namespace gomoku {
namespace {

inline ChessType toLegacy(Stone s) { return static_cast<ChessType>(static_cast<int>(s)); }
inline Stone fromLegacy(ChessType c) { return static_cast<Stone>(static_cast<int>(c)); }

// gomoku::status::* 字符串 → 旧 GameStatus 枚举
GameStatus toLegacyStatus(const std::string& s) {
    if (s == status::kBlackWin) return GameStatus::BlackWin;
    if (s == status::kWhiteWin) return GameStatus::WhiteWin;
    if (s == status::kDraw)     return GameStatus::Draw;
    if (s == status::kIdle)     return GameStatus::Aborted;
    return GameStatus::InProgress;
}

class LegacyStorageGateway final : public IStorageGateway {
public:
    explicit LegacyStorageGateway(std::string root) {
        StorageConfig cfg;
        if (!root.empty()) cfg.dataDir = std::move(root);
        cfg.enabled = true;
        store_.setConfig(cfg);
    }

    // ---- 开关 ----
    bool enabled() const override { return store_.isEnabled(); }
    void setEnabled(bool on) override { store_.setEnabled(on); }

    // ---- 对局生命周期 ----
    void beginGame(const RulesConfig& rules, const Seat& black, const Seat& white) override {
        // 旧接口的 p1Type/p2Type 是历史快照字段，用最小可用值占位；
        // 存档里真正决定回放行为的是名字与 id（见 brief 转换）。
        store_.startGame(rules.boardSize, rules.winLength,
                         black.playerName, white.playerName, 0, 0);
        blackId_ = black.playerId;
        whiteId_ = white.playerId;
    }

    void record(const Move& m) override {
        store_.recordMove(m.r, m.c, toLegacy(m.color));
    }

    void finish(const std::string& s) override {
        if (store_.isInGame()) store_.endGame(toLegacyStatus(s));
    }

    bool inGame() const override { return store_.isInGame(); }

    // ---- 悔棋：只丢记录，棋盘由内核按自己的实现重建 ----
    // 这样存储层完全不需要认识内核的棋盘类型（旧接口的 undoMoves(Board&)
    // 恰恰是「存储耦合具体棋盘」的坏味道，新边界把它切掉了）。
    int undo(int steps, IBoard&) override {
        return store_.dropLastMoves(steps > 0 ? steps : 1);
    }

    int moveCount() const override { return static_cast<int>(store_.moves().size()); }
    bool canUndo() const override { return store_.canUndo(); }

    const std::vector<Move>& history() const override {
        cached_.clear();
        cached_.reserve(store_.moves().size());
        for (const auto& r : store_.moves())
            cached_.push_back(Move{ r.r, r.c, fromLegacy(r.color), r.timestamp });
        return cached_;
    }

    // ---- 残局 ----
    bool saveResume(const std::string& note, const ViewState&) override {
        return store_.saveResume(note);
    }

    bool loadResume(const std::string& id, RulesConfig& rules,
                    std::vector<Move>& moves, std::string& blackId,
                    std::string& whiteId) override {
        // 先按棋局找，再按残局找 —— 两类记录都能直接"继续下"
        GameRecord rec;
        if (!store_.loadResume(id, rec) && !store_.loadGame(id, rec)) return false;

        rules.boardSize = rec.boardSize;
        rules.winLength = rec.winLength;
        rules = rules.normalized();

        moves.clear();
        moves.reserve(rec.moves.size());
        for (const auto& m : rec.moves)
            moves.push_back(Move{ m.r, m.c, fromLegacy(m.color), m.timestamp });

        blackId = playerIdOfType(rec.player1Type);
        whiteId = playerIdOfType(rec.player2Type);
        return true;
    }

    std::vector<std::string> listResumes() const override {
        return store_.listResumes();
    }

    // ---- 目录查询 ----
    std::vector<StoredBrief> listGames() const override {
        return collect(store_.listGames(), /*resume=*/false);
    }

    std::vector<StoredBrief> listResumeBriefs() const override {
        return collect(store_.listResumes(), /*resume=*/true);
    }

    bool loadMoves(const std::string& id, std::vector<Move>& moves,
                   StoredBrief& brief, bool resume) const override {
        GameRecord rec;
        const bool ok = resume ? store_.loadResume(id, rec) : store_.loadGame(id, rec);
        if (!ok) return false;

        moves.clear();
        moves.reserve(rec.moves.size());
        for (const auto& m : rec.moves)
            moves.push_back(Move{ m.r, m.c, fromLegacy(m.color), m.timestamp });

        brief = toBrief(rec);
        return true;
    }

    std::vector<std::pair<std::string, long long>> globalStats() const override {
        GlobalStats s = store_.globalStats();
        return {
            { "totalGames", s.totalGames },
            { "blackWins", s.blackWins },
            { "whiteWins", s.whiteWins },
            { "draws", s.draws },
            { "aborts", s.aborts },
            { "blackTotalMoves", s.blackTotalMoves },
            { "whiteTotalMoves", s.whiteTotalMoves },
        };
    }

private:
    // 旧存档的 playerXType 是"玩家编号"，新架构的玩家是字符串 id。
    // 若存档里保存的是新的字符串 id，也允许直接透传（loadResume 走这条分支）。
    static std::string playerIdOfType(int type) {
        if (type == 1) return "human";
        // 旧编号 → 新 id 的映射表（只影响"载入老存档"这一条路径）
        switch (type) {
            case 2: return "easy-judge";
            case 3: return "puregreed-1.0";
            case 4: return "puregreed-1.1";
            case 5: return "minimax";
            case 6: return "api-ai";
            case 7: return "tactical-max";
            default: return "human";
        }
    }

    static StoredBrief toBrief(const GameRecord& rec) {
        StoredBrief b;
        b.id = rec.id;
        b.boardSize = rec.boardSize;
        b.winLength = rec.winLength;
        b.black = rec.player1Name;
        b.white = rec.player2Name;
        b.blackId = playerIdOfType(rec.player1Type);
        b.whiteId = playerIdOfType(rec.player2Type);
        b.moves = static_cast<int>(rec.moves.size());
        b.note = rec.note;
        return b;
    }

    std::vector<StoredBrief> collect(const std::vector<std::string>& ids, bool resume) const {
        std::vector<StoredBrief> out;
        out.reserve(ids.size());
        for (const auto& id : ids) {
            GameRecord rec;
            const bool ok = resume ? store_.loadResume(id, rec) : store_.loadGame(id, rec);
            if (ok) out.push_back(toBrief(rec));
        }
        return out;
    }

    mutable StorageManager store_;
    std::string blackId_, whiteId_;
    mutable std::vector<Move> cached_;   // history() 需要返回引用，这里做缓存
};

}  // namespace

StoragePtr makeLegacyStorageGateway(const std::string& root) {
    return std::make_unique<LegacyStorageGateway>(root);
}

StoragePtr legacyStorageFactory(std::string root) {
    return makeLegacyStorageGateway(root);
}

}  // namespace gomoku
