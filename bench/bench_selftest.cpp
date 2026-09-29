// ============================================================================
// bench_selftest.cpp - 引擎正确性回归测试套件实现
// 用例设计与断言策略见 bench_selftest.h 头部说明。
// ============================================================================

#include "bench_selftest.h"

#include "../core.h"
#include "../player.h"
#include "../tactical_max.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

// ---- 断言工具 ----
bool expectEq(int actual, int expected, const std::string& what, std::string& detail) {
    if (actual == expected) return true;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s: expect %d, got %d", what.c_str(), expected, actual);
    detail = buf;
    return false;
}

bool expectTrue(bool cond, const std::string& what, std::string& detail) {
    if (cond) return true;
    detail = what;
    return false;
}

// 构造一个空棋盘
std::unique_ptr<Board> makeBoard(int n, int w) {
    auto b = std::make_unique<Board>();
    b->resize(n);
    b->setWinLen(w);
    return b;
}

// 用坐标列表摆子。必须走 place()（维护 emptyCount_）而不是 set()：
// 否则棋盘在 Board 看来仍是"全空"，place() 入口会触发首手随机而非正常决策。
void putStones(Board& b, ChessType c, const std::vector<std::pair<int, int>>& cells) {
    for (const auto& p : cells) b.place(p.first, p.second, c);
}

// ===========================================================================
// T1 窗口索引完整性
// ===========================================================================
bool testWindowIndex(std::string& detail) {
    Judge judge;
    TacticalMax probe(judge);

    // 期望窗口数 = 2*boardSize*line + 2*line*line，其中 line = n - w + 1
    const int cases[][2] = {
        { 15, 5 }, { 15, 4 }, { 6, 4 }, { 8, 5 }, { 10, 6 }, { 20, 5 }, { 4, 4 },
    };
    for (const auto& cs : cases) {
        const int n = cs[0], w = cs[1];
        auto board = makeBoard(n, w);
        putStones(*board, ChessType::Black, { { 0, 0 } });   // 保证棋盘非空
        const ShapeReport rep = probe.analyze(*board, ChessType::Black);
        if (!expectEq(rep.windowCount, probe.planWindowCount(n, w),
                      "windowCount(n=" + std::to_string(n) + ",w=" + std::to_string(w) + ")", detail))
            return false;
    }
    // 退化尺寸（w > n）：不应建立任何窗口，也不应崩溃
    {
        auto board = makeBoard(4, 5);
        putStones(*board, ChessType::Black, { { 0, 0 } });
        const ShapeReport rep = probe.analyze(*board, ChessType::Black);
        if (!expectEq(rep.windowCount, 0, "degenerate(n=4,w=5).windowCount", detail)) return false;
    }
    return true;
}

// ===========================================================================
// T2 活四/冲四判定（bug2 第 1 条的精确回归）
// ===========================================================================
bool testShapeVerdict(std::string& detail) {
    Judge judge;
    TacticalMax probe(judge);
    constexpr int n = 15, w = 5, row = 7;

    auto fiveCount = [&](const std::vector<std::pair<int, int>>& black,
                         const std::vector<std::pair<int, int>>& white) {
        auto board = makeBoard(n, w);
        putStones(*board, ChessType::Black, black);
        putStones(*board, ChessType::White, white);
        return probe.analyze(*board, ChessType::Black);
    };

    // (a) 活四 _XXXX_ ：两个不同的成五点，对手堵不完 → 必须为 2
    {
        const ShapeReport rep = fiveCount({ { row, 5 }, { row, 6 }, { row, 7 }, { row, 8 } }, {});
        if (!expectEq(rep.fivePoints, 2, "live four (XXXX., both ends open)", detail)) return false;
    }
    // (b) 冲四 OXXXX_ ：左端被对手封，只剩一个成五点 → 必须为 1
    {
        const ShapeReport rep = fiveCount({ { row, 5 }, { row, 6 }, { row, 7 }, { row, 8 } },
                                          { { row, 4 } });
        if (!expectEq(rep.fivePoints, 1, "rush four (blocked left)", detail)) return false;
    }
    // (c) 冲四 _XXXXO ：右端被对手封 → 必须为 1
    {
        const ShapeReport rep = fiveCount({ { row, 5 }, { row, 6 }, { row, 7 }, { row, 8 } },
                                          { { row, 9 } });
        if (!expectEq(rep.fivePoints, 1, "rush four (blocked right)", detail)) return false;
    }
    // (d) 断口形 XXX_XX ：结构上有两个窗口指向同一个空位，成五点仍是唯一的 → 必须为 1
    {
        const ShapeReport rep = fiveCount({ { row, 5 }, { row, 6 }, { row, 7 }, { row, 9 }, { row, 10 } }, {});
        if (!expectEq(rep.fivePoints, 1, "broken four (XXX_XX)", detail)) return false;
    }
    // (e) 边界 _XXXX| ：右侧越界，不存在第二个成五点 → 必须为 1
    {
        const ShapeReport rep = fiveCount({ { row, 11 }, { row, 12 }, { row, 13 }, { row, 14 } }, {});
        if (!expectEq(rep.fivePoints, 1, "edge four (no room outside)", detail)) return false;
    }
    // (f) 仅三子 _XXX_ ：尚未到四，不应产生任何成五点
    {
        const ShapeReport rep = fiveCount({ { row, 6 }, { row, 7 }, { row, 8 } }, {});
        if (!expectEq(rep.fivePoints, 0, "live three has no five point", detail)) return false;
    }
    return true;
}

// ===========================================================================
// T3 棋型梯度单调性
// ===========================================================================
bool testScoreGradient(std::string& detail) {
    Judge judge;
    TacticalMax probe(judge);
    constexpr int n = 15, w = 5, row = 7;

    auto scoreOf = [&](const std::vector<std::pair<int, int>>& black,
                       const std::vector<std::pair<int, int>>& white) {
        auto board = makeBoard(n, w);
        putStones(*board, ChessType::Black, black);
        putStones(*board, ChessType::White, white);
        return probe.analyze(*board, ChessType::Black).totalScore;
    };

    // 活四 vs 冲四：活四必须显著更强
    {
        const long long live = scoreOf({ { row, 5 }, { row, 6 }, { row, 7 }, { row, 8 } }, {});
        const long long rush = scoreOf({ { row, 5 }, { row, 6 }, { row, 7 }, { row, 8 } },
                                       { { row, 4 } });
        if (!expectTrue(live > rush * 3, "live four score should dwarf rush four", detail))
            return false;
    }
    // 活三 vs 眠三：量级必须拉开（对应"活三/眠三差 10 倍"的验收口径）
    {
        // 眠三：白子紧贴三子左端（隔一格的"堵"会构成 O_XXXX_ 型活四，用例就错了）
        const long long live  = scoreOf({ { row, 6 }, { row, 7 }, { row, 8 } }, {});
        const long long sleep = scoreOf({ { row, 6 }, { row, 7 }, { row, 8 } }, { { row, 5 } });
        if (!expectTrue(live > sleep * 3, "live three score should dwarf sleep three", detail))
            return false;
    }
    // 单调递增：二 < 三 < 四
    {
        const long long two   = scoreOf({ { row, 7 }, { row, 8 } }, {});
        const long long three = scoreOf({ { row, 6 }, { row, 7 }, { row, 8 } }, {});
        const long long four  = scoreOf({ { row, 5 }, { row, 6 }, { row, 7 }, { row, 8 } }, {});
        if (!expectTrue(two < three && three < four, "two < three < four by score", detail))
            return false;
    }
    return true;
}

// ===========================================================================
// T4 增量状态可逆性
// ===========================================================================
bool testIncremental(std::string& detail) {
    Judge judge;
    TacticalMax probe(judge);

    const int sizes[][2] = { { 15, 5 }, { 8, 4 }, { 10, 6 } };
    const int stepPlan[] = { 5, 20, 60 };

    for (const auto& sz : sizes) {
        for (int steps : stepPlan) {
            // 预置若干棋子，使状态不是全空
            auto board = makeBoard(sz[0], sz[1]);
            putStones(*board, ChessType::Black, { { 0, 0 }, { 1, 1 } });
            putStones(*board, ChessType::White, { { 0, 1 }, { 1, 0 } });

            for (unsigned seed = 1; seed <= 8; ++seed) {
                if (!probe.selfCheckIncremental(*board, steps, seed)) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                                  "state not restored (n=%d,w=%d,steps=%d,seed=%u)",
                                  sz[0], sz[1], steps, seed);
                    detail = buf;
                    return false;
                }
            }
        }
    }
    return true;
}

// ===========================================================================
// T5 战术行为下限
// ===========================================================================
bool testTacticalBehavior(std::string& detail) {
    Judge judge;
    constexpr int n = 15, w = 5;

    // (a) 己方一步成五必须直接拿下
    {
        auto board = makeBoard(n, w);
        putStones(*board, ChessType::Black, { { 7, 5 }, { 7, 6 }, { 7, 7 }, { 7, 8 } });
        putStones(*board, ChessType::White, { { 0, 0 } });
        TacticalMax ai(judge);
        ai.setTimeBudgetMs(300);
        const Pos mv = ai.place(*board, ChessType::Black);
        bool placed = false, won = false;
        if (mv.valid()) placed = board->place(mv.r, mv.c, ChessType::Black);
        if (placed) won = judge.checkWin(*board, mv, ChessType::Black);
        if (!won) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "move=(%d,%d) valid=%d placed=%d won=%d",
                          mv.r, mv.c, (int)mv.valid(), (int)placed, (int)won);
            detail = std::string("must take immediate five when available -> ") + buf;
            return false;
        }
    }
    // (b) 对手一步成五必须堵住
    {
        auto board = makeBoard(n, w);
        putStones(*board, ChessType::White, { { 3, 5 }, { 3, 6 }, { 3, 7 }, { 3, 8 } });
        putStones(*board, ChessType::Black, { { 0, 0 }, { 10, 10 } });
        TacticalMax ai(judge);
        ai.setTimeBudgetMs(300);
        const Pos mv = ai.place(*board, ChessType::Black);
        const bool blocksLeft  = mv.valid() && mv.r == 3 && mv.c == 4;
        const bool blocksRight = mv.valid() && mv.r == 3 && mv.c == 9;
        if (!expectTrue(blocksLeft || blocksRight, "must block opponent's immediate five", detail))
            return false;
    }
    // (c) 对手活四（两端开放的四）已不可双堵，至少不应崩溃并给出合法着法
    {
        auto board = makeBoard(n, w);
        putStones(*board, ChessType::White, { { 5, 5 }, { 5, 6 }, { 5, 7 }, { 5, 8 } });
        putStones(*board, ChessType::Black, { { 0, 0 } });
        TacticalMax ai(judge);
        ai.setTimeBudgetMs(300);
        const Pos mv = ai.place(*board, ChessType::Black);
        if (!expectTrue(mv.valid() && board->place(mv.r, mv.c, ChessType::Black),
                        "must emit legal move against live four", detail)) return false;
    }
    return true;
}

// ===========================================================================
// T6 多尺寸 / 多连珠数鲁棒性（n 子棋适配）
// ===========================================================================
bool testMultiSizeRobustness(std::string& detail) {
    Judge judge;
    const int boards[]  = { 4, 5, 6, 8, 10, 15, 20, 30 };
    const int winLens[] = { 4, 5, 6, 8, 10 };

    for (int n : boards) {
        for (int w : winLens) {
            if (w > n && !(w == 5 && n == 4)) continue;   // 仅额外保留一个退化组合
            auto board = makeBoard(n, w);
            putStones(*board, ChessType::Black, { { 0, 0 } });
            putStones(*board, ChessType::White, { { 1, 1 } });

            TacticalMax ai(judge);
            ai.setTimeBudgetMs(150);
            const Pos mv = ai.place(*board, ChessType::Black);
            const bool legal = mv.valid() && board->inBounds(mv.r, mv.c)
                               && board->at(mv.r, mv.c) == ChessType::None
                               && board->place(mv.r, mv.c, ChessType::Black);
            if (!legal) {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "illegal move at n=%d w=%d", n, w);
                detail = buf;
                return false;
            }
        }
    }
    return true;
}

struct Case {
    const char* name;
    bool (*fn)(std::string&);
};

const Case kCases[] = {
    { "T1 window-index",        testWindowIndex },
    { "T2 shape-verdict",       testShapeVerdict },
    { "T3 score-gradient",      testScoreGradient },
    { "T4 incremental-restore", testIncremental },
    { "T5 tactical-behavior",   testTacticalBehavior },
    { "T6 multi-size",          testMultiSizeRobustness },
};

}  // namespace

int runSelfTests(bool verbose, std::vector<TestResult>* out) {
    int failed = 0;
    for (const Case& c : kCases) {
        std::string detail;
        const bool ok = c.fn(detail);
        if (!ok) ++failed;
        if (verbose)
            std::printf("  [%-4s] %s%s%s\n", ok ? "PASS" : "FAIL", c.name,
                        ok ? "" : "  -> ", ok ? "" : detail.c_str());
        if (out) out->push_back(TestResult{ c.name, ok, detail });
    }
    return failed;
}
