// game_update.c - 游戏输入处理
// 本文件处理鼠标移动和点击事件


#include "game.h"
#include <graphics.h>
#include <math.h>
#include <stdio.h>

void update(Game* pthis) 
{
    // 如果是鼠标移动消息
    if (pthis->msg.message == WM_MOUSEMOVE)
    {
        // 获取鼠标当前屏幕坐标
        int mx = pthis->msg.x;
        int my = pthis->msg.y;
        // 遍历棋盘每个交叉点
        for (int i = 0; i < ROWS; i++) 
        {
            for (int j = 0; j < COLS; j++) 
            {
                // 计算该交叉点的屏幕中心坐标
                int cx = j * GRID_SIZE + XOFFSET;
                int cy = i * GRID_SIZE + YOFFSET;
                // 判断鼠标是否在该格子范围内（以中心为基准，半格为半径）
                if (abs(mx - cx) < GRID_SIZE / 2 && abs(my - cy) < GRID_SIZE / 2)
                {
                    // 更新悬停坐标为该格子
                    pthis->row = i;
                    pthis->col = j;
                    // 找到后立即返回
                    return;
                }
            }
        }
        // 如果未找到有效格子，设为无效坐标
        pthis->row = -1;
        pthis->col = -1;
    }
    // 如果是鼠标左键点击消息
    else if (pthis->msg.message == WM_LBUTTONDOWN)
    {
        // 检查悬停坐标是否在棋盘有效范围内
        if (pthis->row >= 0 && pthis->col >= 0 &&
            pthis->row < ROWS && pthis->col < COLS)
        {
            // 检查该位置是否已有棋子
            if (pthis->map[pthis->row][pthis->col] != None) 
            {
                // 弹出提示框：“此处已有棋子，请选择空位！”
                MessageBox(GetHWnd(), L"此处已有棋子，请选择空位！", L"提示", MB_OK | MB_ICONINFORMATION);
                // 标记本次点击未成功落子
                pthis->hasMovedThisClick = false;
                // 直接返回，不落子
                return;
            }
            // 如果是双人模式
            if (pthis->mode == PVP)
            {
                // 在该位置放置当前玩家的棋子
                pthis->map[pthis->row][pthis->col] = pthis->currentChessType;
                // 如果当前是黑棋，记录步数并打印
                if (pthis->currentChessType == Black) 
                {
                    pthis->blackMoveCount++;
                    system("cls");
                    printf("当前黑棋步数：%d\n", pthis->blackMoveCount);
                }
                // 切换当前玩家（黑变白，白变黑）
                pthis->currentChessType = (ChessType)(-pthis->currentChessType);
                // 标记本次点击成功落子
                pthis->hasMovedThisClick = true;
            }
            // 如果是人机模式
            else {
                // 且当前是人类回合
                if (pthis->isHumanTurn) 
                {
                    // 人类下黑棋
                    pthis->map[pthis->row][pthis->col] = Black;
                    // 记录黑棋步数并清屏打印
                    pthis->blackMoveCount++;
                    system("cls");

                   // printf("当前黑棋步数：%d\n", pthis->blackMoveCount);
                   
                    // 1. 计算分数 (二次函数 y = x^2)
                    int player_score = pthis->blackMoveCount * pthis->blackMoveCount+ pthis->blackMoveCount*3;

                    // 2. 打印步数和分数到控制台
                    printf("黑棋已下 %d 步, 当前得分: %d\n", pthis->blackMoveCount, player_score);

                    // 3. 根据步数打印不同的鼓励话语
                    if (pthis->blackMoveCount < 10)
                    {
                        printf("初窥门径，棋路清晰。\n");
                    }
                    else if (pthis->blackMoveCount < 25) 
                    {
                        printf("布局沉稳，静待时机。\n");
                    }
                    else if (pthis->blackMoveCount < 35) 
                    {
                        printf("先声夺人，气势不凡。\n");
                    }
                    else if (pthis->blackMoveCount < 50)
                    {
                        printf("攻守兼备，滴水不漏。\n");
                    }
                    else if (pthis->blackMoveCount < 60)
                    {
                        printf("妙手频出，暗藏杀机。\n");
                    }
                    else if (pthis->blackMoveCount < 70)
                    {
                        printf("胜负已淡，境界为先。\n");
                    }
                    else if (pthis->blackMoveCount < 80)
                    {
                        printf("百步穿杨，功力深厚。\n");
                    }
                    else if (pthis->blackMoveCount < 90)
                    {
                        printf("静水流深，暗流涌动。\n");
                    }
                    else if (pthis->blackMoveCount < 100)
                    {
                        printf("棋盘虽小，格局宏大。\n");
                    }
                    else if (pthis->blackMoveCount < 111)
                    {
                        printf("传世之作，由此诞生。\n");
                    }
                    else if (pthis->blackMoveCount < 112)
                    {
                        printf("高山流水，此棋不凡。\n");
                    }
                    else if (pthis->blackMoveCount < 113)
                    {
                        printf("仅仅剩下最后一步！\n");
                    }
                    else if (pthis->blackMoveCount < 114)
                    {
                        printf("恭喜你获得秘钥——\n密钥：111111。\n");
                    }
                    printf("----------------------------------\n"); // 分割线，让输出更清晰
                
                    // 标记成功落子
                    pthis->hasMovedThisClick = true;
                }
            }
        }
    }
}