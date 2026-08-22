// ============================================================
// core.h - 核心数据结构与逻辑声明
// 包含：坐标、棋子类型、Board/Judge/Stats 三个类
// （全局可变参数已消除：棋盘尺寸/连珠数由 Board 成员持有，
//   网格布局参数由 UI 成员持有，配置时通过成员方法设置）
// ============================================================
#pragma once
#include <vector>

// 棋子类型
enum class ChessType { None = 0, Black = 1, White = -1 };

// 取对手颜色
inline ChessType opponent(ChessType c) { return static_cast<ChessType>(-static_cast<int>(c)); }

// 棋盘坐标
struct Pos {
    int r = -1;
    int c = -1;
    bool valid() const { return r >= 0 && c >= 0; }   // 是否有效
};

// ---- 棋盘处理类：持有一维棋盘数据，提供落子/读取/判满等基本操作 ----
// 现代化内存管理：内部使用 std::vector<int> 一维数组（r*size_+c 索引），
// 消除手动 new/delete；持有 size_（尺寸）、winLen_（连珠获胜数）、
// emptyCount_（空位计数，使 isFull() 为 O(1)）。
class Board {
public:
    Board();
    ~Board();                                       // 空实现（vector 自动释放），保留以兼容已有声明
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;
    void clear();                                   // 清空棋盘（不改变尺寸/连珠数）
    void resize(int n);                             // 按尺寸 n 重建 n×n 棋盘并清零
    bool place(int r, int c, ChessType color);      // 落子（仅空位成功，成功时 emptyCount_--）
    void set(int r, int c, ChessType color);        // 直接设置（模拟用，不校验空位，不维护 emptyCount_）
    ChessType at(int r, int c) const;               // 读取某位置（越界返回 None）
    bool isFull() const;                            // 棋盘是否已满（O(1)，基于 emptyCount_）
    bool inBounds(int r, int c) const;              // 坐标是否在棋盘内
    int size() const { return size_; }              // 当前棋盘尺寸
    int winLen() const { return winLen_; }          // 连珠获胜数
    void setWinLen(int k) { winLen_ = k; }          // 设置连珠获胜数
private:
    std::vector<int> map_;      // 一维棋盘数组（大小 size_*size_，索引 r*size_+c）
    int size_ = 0;              // 当前棋盘实际尺寸
    int winLen_ = 5;            // 连珠获胜数（默认五子棋）
    int emptyCount_ = 0;        // 空位计数（isFull O(1) 用）
    friend class Judge;         // Judge 直接访问 map_ 进行判定
};

// ---- 统一线段扫描核（供 Judge::checkWin 与 player.cpp 的 inlineCheckN/pointScore 共用）----
// 消除两套独立的连珠统计实现：原 Judge::checkLine（offset 回退法）与 player.cpp 的手写双向计数。
struct LineInfo { int count; bool openStart; bool openEnd; };
// 统计经过 (r,c) 沿方向 (dr,dc) 的连续 color 线段。
// count 包含 (r,c) 本身（调用方需保证 (r,c) 已是 color 或先 set）；
// openStart/openEnd 表示两端紧邻是否为空位（越界或异色视为不开放）。
inline LineInfo scanLine(const Board& board, int r, int c, int dr, int dc, ChessType color) {
    LineInfo info{1, false, false};
    // 正方向延伸
    int nr = r + dr, nc = c + dc;
    while (board.inBounds(nr, nc) && board.at(nr, nc) == color) { info.count++; nr += dr; nc += dc; }
    info.openEnd = board.inBounds(nr, nc) && board.at(nr, nc) == ChessType::None;
    // 反方向延伸
    int pr = r - dr, pc = c - dc;
    while (board.inBounds(pr, pc) && board.at(pr, pc) == color) { info.count++; pr -= dr; pc -= dc; }
    info.openStart = board.inBounds(pr, pc) && board.at(pr, pc) == ChessType::None;
    return info;
}

// ---- 胜负判定类：检查经过某点的连珠情况 ----
class Judge {
public:
    bool checkWin(const Board& b, Pos last, ChessType color) const;  // 检查 last 是否使 color 方成连珠（连数取自 b.winLen()，复用 scanLine）
};

// ---- 数据统计类：记录双方步数 ----
class Stats {
public:
    void reset();                       // 清零计数
    void recordMove(ChessType color);   // 记录一步落子
    int count(ChessType color) const;   // 查询某方步数

private:
    int black_ = 0;   // 黑棋步数
    int white_ = 0;   // 白棋步数
};
