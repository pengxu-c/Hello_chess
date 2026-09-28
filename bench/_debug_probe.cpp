// 临时诊断 v2：完全复刻 selftest 的 unique_ptr 写法，定位 T5 差异
#include "core.h"
#include "player.h"
#include "tactical_max.h"
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

static std::unique_ptr<Board> makeBoard(int n, int w) {
    auto b = std::make_unique<Board>();
    b->resize(n);
    b->setWinLen(w);
    return b;
}

static void putStones(Board& b, ChessType c, const std::vector<std::pair<int, int>>& cells) {
    for (const auto& p : cells) b.set(p.first, p.second, c);
}

int main() {
    Judge judge;
    constexpr int n = 15, w = 5;

    auto board = makeBoard(n, w);
    putStones(*board, ChessType::Black, { { 7, 5 }, { 7, 6 }, { 7, 7 }, { 7, 8 } });
    putStones(*board, ChessType::White, { { 0, 0 } });

    // 打印棋盘实际状态，确认摆子正确
    std::printf("board size=%d winlen=%d\n", board->size(), board->winLen());
    std::printf("(7,5..7,8)=%d,%d,%d,%d  (0,0)=%d\n",
                (int)board->at(7, 5), (int)board->at(7, 6),
                (int)board->at(7, 7), (int)board->at(7, 8), (int)board->at(0, 0));

    TacticalMax probe(judge);
    ShapeReport rep = probe.analyze(*board, ChessType::Black);
    std::printf("analyze: five=%d four=%d windows=%d firstFiveIdx=%d -> (r=%d,c=%d)\n",
                rep.fivePoints, rep.fourPoints, rep.windowCount, rep.firstFive,
                rep.firstFive>=0?rep.firstFive/15:-1, rep.firstFive>=0?rep.firstFive%15:-1);

    TacticalMax ai(judge);
    ai.setTimeBudgetMs(300);
    Pos mv = ai.place(*board, ChessType::Black);
    std::printf("decided move: (%d,%d) valid=%d\n", mv.r, mv.c, mv.valid());

    if (mv.valid()) {
        bool placed = board->place(mv.r, mv.c, ChessType::Black);
        std::printf("placed=%d checkWin=%d\n", (int)placed,
                    placed ? (int)judge.checkWin(*board, mv, ChessType::Black) : 0);
    }
    return 0;
}
