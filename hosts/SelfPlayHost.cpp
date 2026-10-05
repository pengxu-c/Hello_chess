// ============================================================
// hosts/SelfPlayHost.cpp - 无头自对弈宿主
//
// 【为什么它值得单独存在】
//   它证明内核可以脱离任何界面运行：本文件里没有 IView、没有 HTTP、
//   没有终端输出 —— 只有一个状态机在被循环推进。
//   因此它可以被 CI 直接调用做回归：换玩家/换规则只改参数，代码不动。
//
// 用法：Gomoku.exe --selfplay 20 --black tactical-max --white minimax --think 800
// ============================================================
#include "Hosts.h"

#include "../kernel/MatchSession.h"
#include "../plugins/storage/LegacyStorageGateway.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace gomoku {

int SelfPlayHost::run() {
    // 自对弈默认不要记录（会写一堆 data/ 文件），除非显式开启存储
    auto storage = opts_.storage ? makeLegacyStorageGateway(opts_.dataDir)
                                 : makeNullStorageGateway();
    MatchSession session(std::move(storage));
    session.setThinkBudgetMs(opts_.thinkMs);
    session.start();

    const int games = opts_.games < 1 ? 1 : opts_.games;
    int blackWins = 0, whiteWins = 0, draws = 0, unfinished = 0;

    std::printf("[selfplay] %s (black) vs %s (white), %d game(s), think budget %dms\n",
                opts_.black.c_str(), opts_.white.c_str(), games, opts_.thinkMs);
    std::fflush(stdout);

    for (int g = 0; g < games; ++g) {
        session.startGame(opts_.rules, opts_.black, opts_.white, false);

        // 等这一局结束：只读快照，不碰内核内部状态
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
        for (;;) {
            const ViewState s = session.state();
            if (isFinished(s.status)) break;
            if (session.quitting() || std::chrono::steady_clock::now() > deadline) break;
            // 极短睡眠：自对弈不需要界面流畅度，但也不该空转吃满一个核
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        const ViewState s = session.state();
        std::string result;
        if (s.status == status::kBlackWin)      { ++blackWins; result = "black wins"; }
        else if (s.status == status::kWhiteWin) { ++whiteWins; result = "white wins"; }
        else if (s.status == status::kDraw)     { ++draws;     result = "draw"; }
        else                                    { ++unfinished; result = "unfinished"; }

        std::printf("  game %2d: %-10s moves=%d\n", g + 1, result.c_str(), s.moveCount);
        std::fflush(stdout);
    }

    session.requestQuit();
    session.join(3000);

    const int decided = blackWins + whiteWins + draws;
    std::printf("[selfplay] black %d | white %d | draws %d | unfinished %d\n",
                blackWins, whiteWins, draws, unfinished);
    if (decided > 0) {
        const double score = 100.0 * (blackWins + 0.5 * draws) / decided;
        std::printf("[selfplay] %s score = %.1f%%\n", opts_.black.c_str(), score);
    }
    std::fflush(stdout);
    return unfinished == games ? 1 : 0;
}

}  // namespace gomoku
