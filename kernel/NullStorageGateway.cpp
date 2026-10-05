// ============================================================
// kernel/NullStorageGateway.cpp - 空存储实现
//
// 存在的意义：内核不应该因为「没有存储实现」而无法工作或被强耦合。
// 单元测试、纯对局（不记录）、以及第三方只想换内核试试的场景，
// 都直接注入它即可。
// ============================================================
#include "IStorageGateway.h"

namespace gomoku {
namespace {

class NullStorageGateway final : public IStorageGateway {
public:
    bool enabled() const override { return enabled_; }
    void setEnabled(bool on) override { enabled_ = on; }

    void beginGame(const RulesConfig&, const Seat&, const Seat&) override { history_.clear(); }
    void record(const Move& m) override { history_.push_back(m); }
    void finish(const std::string&) override {}
    bool inGame() const override { return false; }

    int undo(int, IBoard&) override { return 0; }   // 不记录 → 不可悔棋
    int moveCount() const override { return static_cast<int>(history_.size()); }
    bool canUndo() const override { return false; }
    const std::vector<Move>& history() const override { return history_; }

    bool saveResume(const std::string&, const ViewState&) override { return false; }
    bool loadResume(const std::string&, RulesConfig&, std::vector<Move>&,
                    std::string&, std::string&) override { return false; }
    std::vector<std::string> listResumes() const override { return {}; }

    std::vector<StoredBrief> listGames() const override { return {}; }
    std::vector<StoredBrief> listResumeBriefs() const override { return {}; }
    bool loadMoves(const std::string&, std::vector<Move>&, StoredBrief&, bool) const override {
        return false;
    }
    std::vector<std::pair<std::string, long long>> globalStats() const override { return {}; }

private:
    bool enabled_ = false;
    std::vector<Move> history_;
};

}  // namespace

StoragePtr makeNullStorageGateway() {
    return std::make_unique<NullStorageGateway>();
}

}  // namespace gomoku
