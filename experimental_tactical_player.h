// ============================================================================
// tactical_player.h —— 新增棋手 Tactical++（独立文件，不需要改动 core.h / player.h）
//
// 定位：补齐 Minimax++ 的两处短板
//   1. 棋型识别：把「连续段 + 端点」换成「定长窗口扫描」，从而识别
//      跳三 X_XX、断口四 XX_XX、嵌空冲四 XX_XXX 等带断口的棋型。
//      —— 这是 Minimax++ / ThreatDetector 都做不到的，因为它们共用
//         scanLine()，而 scanLine 是 `while (... == color)`，遇空格即停。
//   2. 战术搜索：新增 VCF（连续冲四取胜）与 VCT（冲四+活三追胜）递归搜索，
//      不再只靠固定 2 步的 twoStepWin 硬编码规则。
//
// 对外依赖（仅此三项，均来自现有工程，无需修改）：
//   core.h   : ChessType / Pos / Board / Judge
//   player.h : Player 基类（place / isHuman / needsDelay / name）
//   标准库
//
// 调用方式与 Minimax++ 完全一致：
//   Judge judge;
//   TacticalPP ai(judge);
//   Pos mv = ai.place(board, color);
// ============================================================================

#pragma once

#include "core.h"
#include "player.h"

#include <vector>

class TacticalPP : public Player {
public:
    // judge 以引用持有，生命周期必须长于本对象（与 MinimaxPP 相同约定）
    explicit TacticalPP(Judge& judge);

    Pos place(Board& board, ChessType color) override;
    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;

private:
    // ---------- 可调参数（改动后重新编译即可，无需改其他地方）----------
    static constexpr int kSearchDepth = 4;   // alpha-beta 搜索深度（含顶层）
    static constexpr int kRadius      = 2;   // 候选点邻域半径
    static constexpr int kVcfDepth    = 12;  // VCF 连续冲四最大层数（每层=一次冲四+一次必应）
    static constexpr int kVctDepth    = 4;   // VCT 追胜最大层数（分支多，层数要小）
    static constexpr int kMaxCands    = 14;  // alpha-beta 每层最多考虑的候选点数
    static constexpr int kInf         = 100000000;

    Judge& judge_;

    // 局面评估（窗口法，能识别断口棋型）
    int evaluate(const Board& board, ChessType me) const;

    // alpha-beta 递归
    int minimax(Board& board, int depth, int alpha, int beta,
                ChessType cur, ChessType me);

    // 候选着法生成（已有棋子周围 kRadius 内的空位，按启发分排序）
    std::vector<Pos> generateMoves(Board& board, ChessType me);
};
