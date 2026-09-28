// ============================================================================
// tactical_max.h - TacticalMax（7 号棋手，转正自实验棋手 Tactical++）
//
// 定位：本项目本地算法中棋力最强的档位，也是架构可扩展性的样板。
//
// 分层设计（实现细节全部封装在 .cpp 的 PImpl 中，头文件保持稳定）：
//   L0 增量窗口状态  —— 预计算所有定长窗口，make/unmake 只更新 ≤4w 个窗口，
//                        评估从 O(n²·w) 降到 O(1)（Phase 1）
//   L1 棋型评估      —— 窗口掩码 → 成五/活四/冲四/活三/眠三/活二 分级查表，
//                        修复"活三与眠三同分"的梯度断裂（Phase 2）
//   L2 搜索框架      —— 迭代加深 + PVS + Zobrist 置换表 + 杀手/历史排序 +
//                        深度相关候选拓宽 + 硬性时间预算（Phase 3）
//   L3 追胜融合      —— VCF/VCT 与主搜索共享时间预算，叶子做 VCF 静态延伸，
//                        消除地平线效应（Phase 4）
//
// 对外契约与其它棋手完全一致：构造传入 Judge&，调用基类 place() 落子。
// 首手随机由 Player 基类统一处理，本类只实现 chooseMove()。
// ============================================================================
#pragma once

#include "core.h"
#include "player.h"

#include <memory>

class Judge;

class TacticalMax : public Player {
public:
    // judge 以引用持有，生命周期必须长于本对象（与 MinimaxPP 相同约定）
    explicit TacticalMax(Judge& judge);
    ~TacticalMax() override;

    // PImpl 持有 unique_ptr，拷贝语义显式删除
    TacticalMax(const TacticalMax&) = delete;
    TacticalMax& operator=(const TacticalMax&) = delete;

    bool isHuman() const override;
    bool needsDelay() const override;
    const char* name() const override;

    // 单步思考时间预算（毫秒），默认 1500；供测试/低配机器调整
    void setTimeBudgetMs(int ms);

protected:
    Pos chooseMove(Board& board, ChessType color) override;

private:
    struct Impl;                       // 引擎实现（窗口索引 / 增量评估 / 搜索 / TT）
    std::unique_ptr<Impl> impl_;
};