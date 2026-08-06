// game_init.c - 游戏初始化实现
// 本文件处理图形窗口初始化和游戏状态重置
// 包括初始化窗口、游戏状态重置等
// 依赖的头文件：game.h，string.h，time.h，stdlib.h

#include "game.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>

void init(Game* pthis, int w, int h, GameMode mode)
{
    // 使用当前时间初始化随机数种子
    srand((unsigned int)time(NULL));
    // 初始化图形窗口，尺寸为 w x h，并显示控制台
    initgraph(w, h, EX_SHOWCONSOLE);
    // 设置游戏运行标志为 true
    pthis->isRunning = true;
    // 初始化鼠标悬停行坐标为 -1（无效值）
    pthis->row = -1;
    // 初始化鼠标悬停列坐标为 -1（无效值）
    pthis->col = -1;
    // 设置当前游戏模式
    pthis->mode = mode;
    // PVP 模式下，黑棋先手
    pthis->currentChessType = Black;
    // PVC 模式下，人类先手
    pthis->isHumanTurn = true;
    // 初始化本次点击未落子
    pthis->hasMovedThisClick = false;
    // 初始化人类步数为 0
    pthis->humanMoveCount = 0;
    // 初始化黑棋步数为 0
    pthis->blackMoveCount = 0;
    if (pthis->map) {
        for (int i = 0; i < ROWS; i++) delete[] pthis->map[i];
        delete[] pthis->map;
    }
    pthis->map = new int*[ROWS];
    for (int i = 0; i < ROWS; i++) {
        pthis->map[i] = new int[COLS];
        for (int j = 0; j < COLS; j++) pthis->map[i][j] = 0;
    }
}