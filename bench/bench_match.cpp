// ============================================================================
// bench_match.cpp - 自动对弈执行与统计实现
// 设计要点见 bench_match.h 头部。本文件不含任何 AI 算法实现。
// ============================================================================
#include "bench_match.h"

#include "../contracts/PlayerRegistry.h"
#include "../kernel/GomokuRules.h"
#include "../kernel/MatchState.h"
#include "../kernel/SquareBoard.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace gomoku {

std::vector<std::string> benchPlayerIds() {
    std::vector<std::string> ids;
    std::vector<PlayerInfo> catalog = playerCatalog();
    ids.reserve(catalog.size());
    for (const auto& p : catalog) {
        if (p.human) continue;                 // 需要交互
        if (!p.fallbackId.empty()) continue;    // 依赖外部配置（API AI）
        if (!p.ready) continue;
        ids.push_back(p.id);
    }
    return ids;
}

std::string benchPlayerName(const std::string& id) {
    return playerRegistry().displayOf(id);
}

bool isBenchPlayable(const std::string& id) {
    const std::vector<std::string> ids = benchPlayerIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

namespace {

double msBetween(const std::chrono::steady_clock::time_point& a,
                 const std::chrono::steady_clock::time_point& b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// 固定开局（天元）。cfg.fixedOpening 为真时保证可复现。
Coord openingMove(const MatchConfig& cfg) {
    if (cfg.fixedOpening) return Coord{ cfg.boardSize / 2, cfg.boardSize / 2 };

    // 中心正方形内按 seed 取点：与旧实现同风格，但用 splitmix64 保证跨平台一致
    uint64_t s = cfg.seed * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL;
    auto next = [&s]() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    };
    const int span = std::max(1, cfg.boardSize / 3 + 1);
    const int base = (cfg.boardSize - span) / 2;
    const int r = base + static_cast<int>(next() % static_cast<unsigned>(span));
    const int c = base + static_cast<int>(next() % static_cast<unsigned>(span));
    return Coord{ r, c };
}

}  // namespace

// ============================================================================
// 下一整局：内核只负责规则，棋手来自插件注册表。
// ============================================================================
GameRecord playOneGame(const MatchConfig& cfg, IPlayer& p1, IPlayer& p2) {
    GameRecord rec;

    SquareBoard board;
    GomokuRules rules;
    MatchState match(board, rules);
    match.reset(RulesConfig{ cfg.boardSize, cfg.winLen });

    // ---- 开局：黑先，固定或随机 ----
    const Coord first = openingMove(cfg);
    if (!match.applyMove(first.r, first.c)) {
        rec.aborted = true;
        rec.note = "opening move rejected";
        return rec;
    }
    rec.moves.push_back(first);
    rec.moveCount = 1;

    const ThinkBudget budget{ cfg.budgetMs, cfg.budgetMs <= 0 };
    const int maxMoves = cfg.boardSize * cfg.boardSize;

    while (rec.moveCount < maxMoves && !match.over()) {
        IPlayer& actor = (match.turn() == Stone::Black) ? p1 : p2;
        const Stone who = match.turn();

        // 棋手只看到只读快照 —— 与产品运行时完全一致的那条路径
        ViewState view;
        view.boardSize = match.board().size();
        view.winLength = match.rules().winLength;
        view.cells = match.board().toCells();
        view.turn = who;
        view.moveCount = match.moveCount();
        view.history = match.history();

        const auto t0 = std::chrono::steady_clock::now();
        const Decision d = actor.tick(view, budget);
        const double cost = msBetween(t0, std::chrono::steady_clock::now());

        if (!d.ok) {
            rec.aborted = true;
            rec.note = actor.displayName() + " gave no move: " +
                       (d.reason.empty() ? "unknown" : d.reason);
            break;
        }
        if (!match.applyMove(d.move.r, d.move.c)) {
            rec.aborted = true;
            rec.note = actor.displayName() + " returned an illegal move";
            break;
        }

        ++rec.moveCount;
        rec.moves.push_back(Coord{ d.move.r, d.move.c });

        if (who == Stone::Black) {
            rec.blackTotalMs += cost;
            rec.blackMaxMs = std::max(rec.blackMaxMs, cost);
        } else {
            rec.whiteTotalMs += cost;
            rec.whiteMaxMs = std::max(rec.whiteMaxMs, cost);
        }
    }

    rec.winner = match.over() ? match.winner() : Stone::Empty;
    return rec;
}

MatchSummary runMatch(const MatchConfig& cfg, bool verbose) {
    MatchSummary sum;
    int finished = 0;

    const PlayerContext ctx{ nullptr, cfg.boardSize, cfg.winLen, cfg.budgetMs };

    for (int g = 0; g < cfg.games; ++g) {
        PlayerPtr p1 = createPlayer(cfg.p1, ctx, nullptr);
        PlayerPtr p2 = createPlayer(cfg.p2, ctx, nullptr);
        if (!p1 || !p2) {
            ++sum.aborted;
            if (verbose) std::printf("  game %2d: ABORTED (player plugin unavailable)\n", g + 1);
            continue;
        }

        const GameRecord rec = playOneGame(cfg, *p1, *p2);
        ++sum.games;

        if (rec.aborted) {
            ++sum.aborted;
            if (verbose) std::printf("  game %2d: ABORTED (%s)\n", g + 1, rec.note.c_str());
            continue;
        }

        if (rec.winner == Stone::Black)      ++sum.p1Wins;
        else if (rec.winner == Stone::White) ++sum.p2Wins;
        else                                 ++sum.draws;

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
                        (rec.winner == Stone::Black) ? "BLACK" :
                        (rec.winner == Stone::White) ? "WHITE" : "DRAW",
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

}  // namespace gomoku
