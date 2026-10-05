// ============================================================
// kernel/GomokuRules.cpp - 标准无禁手五子棋规则
//
// 判定方式：以最后一手为中心，沿 4 个方向（横/竖/两条斜线）双向延伸，
// 统计连续同色总数是否达到 winLength。单次 O(winLength)，
// 比全盘扫描快一个数量级，且天然支持「恰好达成」与「超过达成」。
// ============================================================
#include "GomokuRules.h"

namespace gomoku {

bool GomokuRules::legal(const IBoard& board, int r, int c, Stone who) const {
    if (who == Stone::Empty) return false;
    if (!board.inBounds(r, c)) return false;
    return board.at(r, c) == Stone::Empty;
}

bool GomokuRules::wins(const IBoard& board, int r, int c, Stone who, int winLength) const {
    if (who == Stone::Empty || winLength < 1) return false;
    if (!board.inBounds(r, c)) return false;

    // 四个方向：横、竖、右下、右上
    static constexpr int kDr[4] = { 0, 1, 1, 1 };
    static constexpr int kDc[4] = { 1, 0, 1, -1 };

    for (int d = 0; d < 4; ++d) {
        // 从 (r,c) 本身算起，向两侧各延伸一次。
        // 两个方向刻意分开写，而不是用一个 sign 变量折叠成一个循环：
        // 折叠写法一旦写错极难察觉（本项目就踩过一次，表现为"竖向五连判不出"）。
        int count = 1;

        for (int nr = r + kDr[d], nc = c + kDc[d];          // 正方向
             board.inBounds(nr, nc) && board.at(nr, nc) == who;
             nr += kDr[d], nc += kDc[d]) {
            ++count;
        }
        for (int nr = r - kDr[d], nc = c - kDc[d];          // 反方向
             board.inBounds(nr, nc) && board.at(nr, nc) == who;
             nr -= kDr[d], nc -= kDc[d]) {
            ++count;
        }

        if (count >= winLength) return true;
    }
    return false;
}

}  // namespace gomoku
