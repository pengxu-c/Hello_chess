#pragma once
// game.h - 游戏核心数据结构与函数声明
// 本文件定义游戏所需的所有数据类型、常量和函数原型



#include <stdbool.h>
#include <graphics.h>

// 棋盘常量定义
inline int ROWS = 15;
inline int COLS = 15;
inline int GRID_SIZE = 38; // 每个格子的大小（像素）
inline int XOFFSET = 213; // 棋盘左上角在窗口中的 X、Y 坐标偏移量
inline int YOFFSET = 34;  

// 棋子类型，采用枚举类型更加直观
enum ChessType
{
    None = 0,   // 表示该位置没有棋子
    Black = 1,  // 表示黑棋，由先手方或人类玩家使用
    White = -1  // 表示白棋，由后手方或 AI 使用
};

// 游戏模式
enum GameMode 
{
    PVP,        // 双人对战模式
    PVC_EASY,   // 人机对战 - 简单模式
    PVC_EASIER,    // 更简单模式
    PVC_HARD    // 人机对战 - 困难模式（智能策略）
};

// 游戏状态结构
struct Game 
{
    int** map;            // 二维数组，存储棋盘上每个位置的棋子类型
    bool isRunning;                 // 布尔值，标记游戏是否正在运行
    ExMessage msg;                  // 存储 EasyX 消息（鼠标点击、移动）
    int row, col;                   // 当前鼠标悬停位置对应的棋盘逻辑坐标（行、列）
    ChessType currentChessType;     // 当前轮到哪一方下子（仅 PVP 模式使用）
    GameMode mode;                  // 当前选择的游戏模式
    bool isHumanTurn;               // 是否轮到人类玩家（仅 PVC 模式使用）
    bool hasMovedThisClick;         // 标记本次鼠标点击是否成功落子
    int humanMoveCount;             // 记录人类玩家已下子的步数
    int blackMoveCount;             // 记录黑棋（先手）的总步数，用于控制台显示
};

// 函数声明
GameMode selectMode();              // 在控制台让用户选择游戏模式
void init(Game* pthis, int w, int h, GameMode mode);  // 初始化图形窗口和游戏状态
void render(Game* pthis);           // 绘制棋盘、棋子和鼠标提示圈
void update(Game* pthis);           // 处理鼠标移动和点击事件
bool judge(Game* pthis, ChessType color);  // 判断指定颜色是否形成六子连珠
bool judge2(Game* pthis, ChessType color);   // 五连判定
bool checkLine(Game* pthis, int r, int c, int dr, int dc, int chess);  // 检查从某点沿某方向是否有连续 6 个相同棋子
void aiMoveEasy(Game* pthis);       // 实现简单 AI 落子逻辑
void aiMoveEasyEasy(Game* pthis);   //超简单AI
void aiMoveHard(Game* pthis);       // 实现困难 AI 落子逻辑
bool canBlockFive(Game* pthis, int r, int c);  // 检测某个空位是否能阻挡获胜
bool isBoardFull(Game* pthis);//检查棋盘是否已满