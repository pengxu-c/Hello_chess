// ============================================================
// plugins/players/NativePlayers.h - 原生玩家示例（不依赖任何旧引擎）
//
// 这两个类存在的意义是「演示新增玩家有多便宜」：
//   * 一个类、一个 tick 方法，没有任何注册样板、没有单例、没有宏。
//   * 不包含 core.h / player.h，只用契约层的只读快照 —— 它们甚至不知道
//     棋盘是怎么存的，因此换棋盘实现也不影响它们。
//
// 想看「怎么把已有的重型 AI 引擎接进来」请看 plugins/players/EnginePlayers.h。
// ============================================================
#pragma once
#include "../../contracts/IPlayer.h"
#include <cstdint>

namespace gomoku {

// ---- 随机玩家：最弱的陪练，从空位里均匀随机挑一个 ----
// 用途：新人读代码的起点；也常用来确认「界面点击 → 内核 → 落子」链路是通的。
class RandomPlayer final : public IPlayer {
public:
    explicit RandomPlayer(uint64_t seed = 0);

    std::string id() const override { return "random"; }
    std::string displayName() const override { return "Random"; }
    bool async() const override { return false; }   // 极快，不需要线程

    Decision tick(const ViewState& state, const ThinkBudget& budget) override;

private:
    uint64_t rng_;
};

// ---- 贪心玩家：只看一步，优先延长自己的线，其次堵对方 ----
// 实现刻意写得很短（约 40 行），证明「不需要框架」也能成为一个合格的插件。
class GreedyPlayer final : public IPlayer {
public:
    std::string id() const override { return "greedy"; }
    std::string displayName() const override { return "Greedy"; }
    bool async() const override { return false; }

    Decision tick(const ViewState& state, const ThinkBudget& budget) override;

private:
    // 评估在 (r,c) 落 who 的价值：四个方向的连续长度加权求和
    static long long pointValue(const ViewState& s, int r, int c, Stone who);
};

}  // namespace gomoku
