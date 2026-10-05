// ============================================================
// kernel/MatchState.cpp - 状态机实现
// ============================================================
#include "MatchState.h"

namespace gomoku {

MatchState::MatchState(IBoard& board, const IRules& rules)
    : board_(board), rules_(rules) {
    cfg_ = RulesConfig{}.normalized();
}

void MatchState::reset(const RulesConfig& rules) {
    cfg_ = rules.normalized();
    board_.reset(cfg_.boardSize);
    turn_ = Stone::Black;      // 黑先手
    over_ = false;
    winner_ = Stone::Empty;
    blackMoves_ = 0;
    whiteMoves_ = 0;
    history_.clear();
    ++matchId_;                // 新的一局：界面据此刷新（不要用步数判断）
}

bool MatchState::applyMove(int r, int c) {
    if (over_) return false;
    if (!rules_.legal(board_, r, c, turn_)) return false;

    const Stone mover = turn_;
    if (!board_.place(r, c, mover)) return false;

    history_.push_back(Move{ r, c, mover, 0 });
    if (mover == Stone::Black) ++blackMoves_; else ++whiteMoves_;

    // 先判胜，再判和，最后换手 —— 顺序即规则优先级
    if (rules_.wins(board_, r, c, mover, cfg_.winLength)) {
        over_ = true;
        winner_ = mover;
    } else if (board_.isFull()) {
        over_ = true;
        winner_ = Stone::Empty;   // 和棋
    } else {
        turn_ = opponent(mover);
    }
    return true;
}

int MatchState::undo(int n) {
    if (n <= 0 || history_.empty()) return 0;
    const int actual = (n > static_cast<int>(history_.size()))
                           ? static_cast<int>(history_.size()) : n;
    history_.resize(history_.size() - static_cast<size_t>(actual));
    replayFromHistory();
    return actual;
}

void MatchState::rebuild(const std::vector<Move>& moves, int upTo) {
    const int total = static_cast<int>(moves.size());
    const int n = (upTo < 0 || upTo > total) ? total : upTo;
    history_.assign(moves.begin(), moves.begin() + n);
    replayFromHistory();
}

// 以 history_ 为唯一事实来源重建棋盘与回合。
// 重建而非增量撤销：任何棋盘实现都能正确回退，且不会因多次悔棋累积误差。
void MatchState::replayFromHistory() {
    board_.reset(cfg_.boardSize);
    turn_ = Stone::Black;
    over_ = false;
    winner_ = Stone::Empty;
    blackMoves_ = 0;
    whiteMoves_ = 0;

    for (const auto& m : history_) {
        if (over_) break;                       // 终局后的记录理论上不存在，防御性截断
        if (!rules_.legal(board_, m.r, m.c, turn_)) break;

        const Stone mover = turn_;
        board_.place(m.r, m.c, mover);
        if (mover == Stone::Black) ++blackMoves_; else ++whiteMoves_;

        if (rules_.wins(board_, m.r, m.c, mover, cfg_.winLength)) {
            over_ = true;
            winner_ = mover;
        } else if (board_.isFull()) {
            over_ = true;
            winner_ = Stone::Empty;
        } else {
            turn_ = opponent(mover);
        }
    }
}

}  // namespace gomoku
