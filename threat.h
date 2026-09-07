// ============================================================
// threat.h - 必胜/必防威胁检测类声明
//
// 统一必胜（己方）与必防（对方）判定。核心是"威胁计数"：
//   落 (r,c) 后，把「成连位」与「真活三」各自折算成一个威胁单位，
//   威胁数 >= 2 即强制胜（两步内必胜）。
//
// 覆盖的强制胜形态（等价于对方堵不完）：
//   活四（2 成连位）、双冲四（2 成连位）、四三（1 成连位+1 活三）、双活三（2 活三）
//
// 折算规则：
//   成连位 = n-1 连（冲四/活四）的开放端，每个计 1
//   真活三 = n-2 连且两端开放、且至少一端再往外一格为空（能延伸成活四），每个计 1
//   （假活三两端都只能成眠四，不构成强制胜，故不折算）
//
// 复杂度：一次落子 + 4 方向 scanLine 即得威胁数 O(winLen)；
// 全部检测限定在已有棋子邻域候选内，总 O(候选)。
// ============================================================
#pragma once
#include "core.h"
#include <vector>

class ThreatDetector {
public:
    explicit ThreatDetector(Board& board);   // 持有棋盘引用（调用期有效，不拥有）

    // 威胁计数：模拟落 (r,c) 为 color，返回「成连位 + 折算真活三」的数量。
    // >= 2 表示落子后形成强制胜（两道威胁对方堵不完）。调用后恢复棋盘。
    int threatCount(int r, int c, ChessType color) const;

    // 1 步必胜：color 落子即成的首个空位（无效表示不存在）
    Pos oneStepWin(ChessType color) const;

    // 2 步必胜：color 落子后 threatCount >= 2 且对方无 1 步成连反制的首个位
    Pos twoStepWin(ChessType color, ChessType opp) const;

    // 必防：对方落子后 threatCount >= 2 的所有位（抢对方强制胜的第一步位）
    std::vector<Pos> mustDefend(ChessType opp) const;

    // 候选空位生成：已有棋子周围 radius 内空位（去重），空棋盘返回空。
    // 任一威胁位必然紧邻已有棋子，故限定邻域可大幅收窄扫描（论证见实现）。
    static std::vector<Pos> nearbyEmpties(const Board& board, int radius = 2);

private:
    Board& board_;
};