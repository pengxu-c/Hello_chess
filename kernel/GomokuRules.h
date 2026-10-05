// ============================================================
// kernel/GomokuRules.h - 规则契约 + 五子棋规则实现
//
// 规则被抽成接口，是为了让「换玩法」不动内核其余部分：
//   想加禁手 / 六子棋 / 有禁手的三三禁手 → 新写一个 IRules 实现即可，
//   对局编排（MatchSession）、玩家插件、界面插件全部无感。
//
// 规则是纯函数式的：不持有状态、不改棋子，只读棋盘 + 问两个问题
//   「这一步合法吗」「这一步赢了吗」。
// ============================================================
#pragma once
#include "IBoard.h"

namespace gomoku {

class IRules {
public:
    virtual ~IRules() = default;

    // 这一步是否合法（越界/占用/规则禁手等）
    virtual bool legal(const IBoard& board, int r, int c, Stone who) const = 0;

    // 刚落下的 (r,c) 是否让 who 达成连珠（连珠数来自 winLength）
    virtual bool wins(const IBoard& board, int r, int c, Stone who, int winLength) const = 0;
};

// ---- 标准无禁手五子棋（连珠数可配，因此也直接支持六子棋等） ----
class GomokuRules final : public IRules {
public:
    bool legal(const IBoard& board, int r, int c, Stone who) const override;
    bool wins(const IBoard& board, int r, int c, Stone who, int winLength) const override;
};

}  // namespace gomoku
