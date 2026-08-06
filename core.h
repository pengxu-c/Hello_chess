// ============================================================
// core.h - 核心数据结构与逻辑声明
// 包含：棋盘参数、坐标、棋子类型、Board/Judge/Stats 三个类
// ============================================================
#pragma once

// ---- 棋盘全局参数（inline 变量，跨翻译单元唯一）----
inline int ROWS = 15;       // 棋盘行数
inline int COLS = 15;       // 棋盘列数
inline int GRID_SIZE = 38;  // 每格像素大小
inline int XOFFSET = 213;   // 棋盘左上角 X 偏移
inline int YOFFSET = 34;    // 棋盘左上角 Y 偏移

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

// ---- 棋盘处理类：持有二维棋盘数据，提供落子/读取/判满等基本操作 ----
class Board {
public:
    Board();
    ~Board();
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;
    void clear();                                   // 清空棋盘
    bool place(int r, int c, ChessType color);      // 落子（仅空位成功）
    void set(int r, int c, ChessType color);        // 直接设置（模拟用，不校验空位）
    ChessType at(int r, int c) const;               // 读取某位置
    bool isFull() const;                            // 棋盘是否已满
    bool inBounds(int r, int c) const;              // 坐标是否在棋盘内
private:
    int** map_ = nullptr;   // 二维棋盘数组（动态分配）
    friend class Judge;     // Judge 直接访问 map_ 进行判定
};

// ---- 胜负判定类：检查经过某点的连珠情况 ----
class Judge {
public:
    bool checkWin(const Board& b, Pos last, ChessType color, int length = 6) const;  // 检查 length 连
    bool checkFive(const Board& b, Pos last, ChessType color) const;                  // 检查五连（AI 防守用）
private:
    bool checkLine(const Board& b, int r, int c, int dr, int dc, int chess, int length) const;  // 从某点沿方向连续 length 个
};

// ---- 数据统计类：记录双方步数 ----
class Stats {
public:
    void reset();                       // 清零计数
    void recordMove(ChessType color);   // 记录一步落子
    int count(ChessType color) const;   // 查询某方步数
    void printProgress() const;         // 打印当前进度
private:
    int black_ = 0;   // 黑棋步数
    int white_ = 0;   // 白棋步数
};
