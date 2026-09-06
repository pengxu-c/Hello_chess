// ============================================================
// threat.h - 必胜/必防威胁检测类声明
//
// 统一必胜（己方）与必防（对方）判定，取代 player.cpp 中
// findWinMove / findOppOneStepWin / findOppCriticalThreats /
// findMustDefend 四个职责重叠、语义层叠的散落函数。
//
// 核心抽象：成连位计数 countWinSpots —— 模拟落 (r,c) 后，新增
// "再落一子即成 winLen 连"的空位数量。由此推导出统一威胁层级：
//   1 步必胜：落子即成 winLen 连
//   2 步必胜：落子后成连位 >= 2（成活四等双威胁），且对方无 1 步反制
//   必防    ：必胜类的对方视角复用（抢占对方必胜的第一步位）
//   3 步必防：落子后形成 >= 2 个活三（双活三创建位，超出 2 步框架的额外层）
//
// 复杂度：成连位计数沿 4 方向 scanLine 为 O(winLen)；
// 全部威胁检测均限定在已有棋子邻域候选内，总 O(候选)，
// 消除旧实现 findWinMove 中外层候选 × 内层候选的 O(候选²)。
// ============================================================
#pragma once
#include "core.h"
#include <vector>

class ThreatDetector {
public:
    explicit ThreatDetector(Board& board);   // 持有棋盘引用（调用期有效，不拥有）

    // 成连位计数：模拟落 (r,c) 为 color，返回新增成连位数量。
    // 成连位 = 再落一子 color 即成 winLen 连的空位；调用后恢复棋盘。
    int countWinSpots(int r, int c, ChessType color) const;

    // 1 步必胜：color 落子即成的首个空位（无效表示不存在）
    Pos oneStepWin(ChessType color) const;

    // 2 步必胜：color 落子后成连位 >= 2 且对方无 1 步成连反制的首个位
    Pos twoStepWin(ChessType color, ChessType opp) const;

    // 对方 2 步必胜第一步位集合（落子后成连位 >= 2）：抢占以阻止对方形成活四等
    std::vector<Pos> mustBlockTwo(ChessType opp) const;

    // 对方 3 步必防：落子后形成 >= 2 活三 的位集合（双活三创建位）
    std::vector<Pos> mustBlockDoubleThree(ChessType opp) const;

    // 候选空位生成：已有棋子周围 radius 内空位（去重），空棋盘返回空。
    // 任一成连位必然紧邻已有棋子，故限定邻域可大幅收窄扫描（论证见实现）。
    static std::vector<Pos> nearbyEmpties(const Board& board, int radius = 2);

private:
    Board& board_;
};