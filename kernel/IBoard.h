// ============================================================
// kernel/IBoard.h - 棋盘契约（内核的内部边界）
//
// 「棋盘维护」被单独隔离在这一层：任何棋盘实现（方形、六边形、立体、
// 甚至蒙特卡洛用的稀疏棋盘）只要满足本接口，就能直接替换掉 SquareBoard，
// 而上层的规则、玩家、界面一行都不用改。
//
// 规则判定需要「沿方向连续同色长度」，所以接口以坐标访问为主，
// 不暴露任何内部存储布局 —— 换实现不会漏出细节。
// ============================================================
#pragma once
#include "../contracts/GameTypes.h"

namespace gomoku {

class IBoard {
public:
    virtual ~IBoard() = default;

    virtual int size() const = 0;                       // 边长 N（N×N）
    virtual void reset(int n) = 0;                      // 重建为 n×n 并清空
    virtual void clear() = 0;                           // 清空（尺寸不变）

    virtual bool inBounds(int r, int c) const = 0;
    virtual Stone at(int r, int c) const = 0;           // 越界返回 Empty

    virtual bool place(int r, int c, Stone s) = 0;      // 仅空位可落子
    virtual bool unset(int r, int c) = 0;               // 撤子（与 place 配对）

    virtual bool isFull() const = 0;                    // O(1)
    virtual bool isEmpty() const = 0;                   // O(1)
    virtual int  stoneCount() const = 0;                // 已有棋子数 = N² - 空位数

    // 导出为一维数组（r*size()+c），供 ViewState 快照使用
    virtual std::vector<int> toCells() const = 0;
};

}  // namespace gomoku
