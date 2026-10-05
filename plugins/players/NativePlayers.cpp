// ============================================================
// plugins/players/NativePlayers.cpp - 原生玩家实现
// ============================================================
#include "NativePlayers.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

namespace gomoku {

// ============================================================
// RandomPlayer
// ============================================================
RandomPlayer::RandomPlayer(uint64_t seed) {
    if (seed == 0) {
        seed = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }
    // splitmix64 播种，避免低位规律
    rng_ = seed + 0x9E3779B97F4A7C15ULL;
}

Decision RandomPlayer::tick(const ViewState& state, const ThinkBudget&) {
    std::vector<Coord> empty;
    empty.reserve(static_cast<size_t>(state.boardSize) * state.boardSize);
    for (int r = 0; r < state.boardSize; ++r)
        for (int c = 0; c < state.boardSize; ++c)
            if (state.at(r, c) == Stone::Empty) empty.push_back({ r, c });

    if (empty.empty()) return Decision::pass("board full");

    // xorshift64*：够随机、够快、无第三方依赖
    rng_ ^= rng_ >> 12;
    rng_ ^= rng_ << 25;
    rng_ ^= rng_ >> 27;
    const uint64_t idx = (rng_ * 0x2545F4914F6CDD1DULL) % empty.size();
    return Decision::at(empty[static_cast<size_t>(idx)].r,
                        empty[static_cast<size_t>(idx)].c);
}

// ============================================================
// GreedyPlayer
// ============================================================
long long GreedyPlayer::pointValue(const ViewState& s, int r, int c, Stone who) {
    static constexpr int kDr[4] = { 0, 1, 1, 1 };
    static constexpr int kDc[4] = { 1, 0, 1, -1 };

    long long total = 0;
    for (int d = 0; d < 4; ++d) {
        // 假设 (r,c) 已经是 who，统计双向连续长度，再把两端开放性作为权重。
        int count = 1;
        int openEnds = 0;
        for (int sign = 1; sign >= -1; sign -= 2) {
            int nr = r + kDr[d] * sign;
            int nc = c + kDc[d] * sign;
            while (s.at(nr, nc) == who) {
                ++count;
                nr += kDr[d] * sign;
                nc += kDc[d] * sign;
            }
            if (s.at(nr, nc) == Stone::Empty) ++openEnds;
        }
        if (count >= 5) {
            total += 1000000;
        } else if (count == 4) {
            total += (openEnds >= 1) ? 50000 : 4000;
        } else if (count == 3) {
            total += (openEnds == 2) ? 3000 : 300;
        } else if (count == 2) {
            total += (openEnds == 2) ? 120 : 30;
        } else {
            total += openEnds * 2;
        }
    }
    return total;
}

Decision GreedyPlayer::tick(const ViewState& state, const ThinkBudget&) {
    const Stone me = state.turn;
    const Stone foe = opponent(me);

    long long best = -1;
    Coord bestMove = kNoCoord;

    for (int r = 0; r < state.boardSize; ++r) {
        for (int c = 0; c < state.boardSize; ++c) {
            if (state.at(r, c) != Stone::Empty) continue;

            // 只看已有棋子附近的空位：既省时间，也避免走出毫无意义的远点
            bool nearStone = false;
            for (int dr = -2; dr <= 2 && !nearStone; ++dr)
                for (int dc = -2; dc <= 2; ++dc)
                    if (state.at(r + dr, c + dc) != Stone::Empty) { nearStone = true; break; }
            if (!nearStone && state.moveCount > 0) continue;

            // 攻守同权：既看自己成线，也看堵对方
            const long long score = pointValue(state, r, c, me) +
                                    pointValue(state, r, c, foe) * 9 / 10;
            if (score > best) {
                best = score;
                bestMove = { r, c };
            }
        }
    }

    if (!bestMove.valid()) return Decision::pass("no candidate");
    return Decision::at(bestMove.r, bestMove.c);
}

}  // namespace gomoku
