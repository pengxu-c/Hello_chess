// ============================================================================
// bench_match.cpp - 自动对弈执行与统计实现
// 设计要点见 bench_match.h 头部。本文件不含任何 AI 算法实现。
// ============================================================================

#include "bench_match.h"

#include "../player.h"
#include "../tactical_max.h"
#include "../ui.h"
#include "../player_registry.h"

#include <algorithm>
#include <chrono>
#include <random>

// 显示名 / 可用编号 / 构建统一委托给玩家注册表（唯一注册点见 player_registry.cpp）
const char* playerIdName(int id) {
    const PlayerInfo* p = playerRegistry().find(id);
    return p ? p->idName.c_str() : "Unknown";
}

const std::vector<int>& benchPlayerIds() {
    static const std::vector<int> ids = [] {
        std::vector<int> v;
        for (const PlayerInfo& info : playerRegistry().catalog())
            if (info.playable) v.push_back(info.id);
        return v;
    }();
    return ids;
}

bool isPlayableId(int id) {
    return playerRegistry().isPlayable(id);
}

std::unique_ptr<Player> createPlayerById(int id, Judge& judge, UI*) {
    if (!playerRegistry().isPlayable(id)) return nullptr;
    PlayerContext ctx{ &judge, nullptr, nullptr };
    return playerRegistry().create(id, ctx);
}

namespace {

// 时间预算只对暴露了该接口的棋手生效（TacticalMax）
void applyBudget(Player* p, int budgetMs) {
    if (budgetMs <= 0) return;
    if (auto* tm = dynamic_cast<TacticalMax*>(p)) tm->setTimeBudgetMs(budgetMs);
}

double msBetween(const std::chrono::steady_clock::time_point& a,
                 const std::chrono::steady_clock::time_point& b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

}  // namespace

GameRecord playOneGame(const MatchConfig& cfg, Player& p1, Player& p2) {
    GameRecord rec;
    Judge judge;

    Board board;
    board.resize(cfg.boardSize);
    board.setWinLen(cfg.winLen);

    const int maxMoves = cfg.boardSize * cfg.boardSize;

    // ---- 开局 ----
    int firstR = cfg.boardSize / 2, firstC = cfg.boardSize / 2;
    if (!cfg.fixedOpening) {                      // 中心区随机起点
        std::mt19937 rng(cfg.seed);
        const int span = std::max(1, cfg.boardSize / 3 + 1);
        firstR = static_cast<int>(rng() % static_cast<unsigned>(span)) + (cfg.boardSize - span) / 2;
        firstC = static_cast<int>(rng() % static_cast<unsigned>(span)) + (cfg.boardSize - span) / 2;
    }
    if (!board.place(firstR, firstC, ChessType::Black)) {
        rec.aborted = true;
        rec.note = "opening move rejected";
        return rec;
    }
    rec.moveCount = 1;
    rec.moves.push_back({ firstR, firstC });

    ChessType turn = ChessType::White;
    while (rec.moveCount < maxMoves) {
        Player& actor = (turn == ChessType::Black) ? p1 : p2;

        const auto t0 = std::chrono::steady_clock::now();
        const Pos mv = actor.place(board, turn);
        const double cost = msBetween(t0, std::chrono::steady_clock::now());

        if (!mv.valid() || !board.place(mv.r, mv.c, turn)) {
            rec.aborted = true;
            rec.note = std::string("illegal move from ") +
                       ((turn == ChessType::Black) ? "black" : "white");
            break;
        }
        ++rec.moveCount;
        rec.moves.push_back(mv);

        if (turn == ChessType::Black) {
            rec.blackTotalMs += cost;
            rec.blackMaxMs = std::max(rec.blackMaxMs, cost);
        } else {
            rec.whiteTotalMs += cost;
            rec.whiteMaxMs = std::max(rec.whiteMaxMs, cost);
        }

        if (judge.checkWin(board, mv, turn)) { rec.winner = turn; break; }
        if (board.isFull()) break;
        turn = opponent(turn);
    }
    return rec;
}

MatchSummary runMatch(const MatchConfig& cfg, bool verbose) {
    Judge judge;
    MatchSummary sum;
    int finished = 0;

    for (int g = 0; g < cfg.games; ++g) {
        auto p1 = createPlayerById(cfg.p1, judge, nullptr);
        auto p2 = createPlayerById(cfg.p2, judge, nullptr);
        if (!p1 || !p2) { ++sum.aborted; continue; }

        applyBudget(p1.get(), cfg.budgetMs);
        applyBudget(p2.get(), cfg.budgetMs);

        const GameRecord rec = playOneGame(cfg, *p1, *p2);
        ++sum.games;

        if (rec.aborted) {
            ++sum.aborted;
            if (verbose) std::printf("  game %2d: ABORTED (%s)\n", g + 1, rec.note.c_str());
            continue;
        }

        if (rec.winner == ChessType::Black)      ++sum.p1Wins;
        else if (rec.winner == ChessType::White) ++sum.p2Wins;
        else                                     ++sum.draws;

        const int bMoves = (rec.moveCount + 1) / 2;
        const int wMoves = rec.moveCount / 2;
        if (bMoves > 0) {
            sum.p1AvgMs += rec.blackTotalMs / bMoves;
            sum.p1MaxMs  = std::max(sum.p1MaxMs, rec.blackMaxMs);
        }
        if (wMoves > 0) {
            sum.p2AvgMs += rec.whiteTotalMs / wMoves;
            sum.p2MaxMs  = std::max(sum.p2MaxMs, rec.whiteMaxMs);
        }
        sum.avgMoves += rec.moveCount;
        ++finished;

        if (verbose) {
            std::printf("  game %2d: %-6s | %3d moves | black %7.1fms | white %7.1fms\n",
                        g + 1,
                        (rec.winner == ChessType::Black) ? "BLACK" :
                        (rec.winner == ChessType::White) ? "WHITE" : "DRAW",
                        rec.moveCount, rec.blackTotalMs, rec.whiteTotalMs);
        }
    }

    if (finished > 0) {
        sum.avgMoves /= finished;
        sum.p1AvgMs  /= finished;
        sum.p2AvgMs  /= finished;
        const int total = sum.p1Wins + sum.p2Wins + sum.draws;
        sum.winRate = total > 0 ? (sum.p1Wins + 0.5 * sum.draws) / total : 0.0;
    }
    return sum;
}
