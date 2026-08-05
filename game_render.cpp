// game_render.c - 游戏渲染实现
// 本文件负责绘制棋盘、棋子和鼠标提示
// 绘制棋盘：绘制网格线
// 绘制棋子：根据棋子的颜色，在对应位置画一个实心圆
// 绘制鼠标提示：当鼠标悬停在一个有效位置时，在该位置画一个空心圆
//绘制的棋盘用于以防万一，如果棋盘图片加载失败，可以使用这段代码绘制棋盘，不会影响游戏逻辑


#include "game.h"
#include <graphics.h>

void render(Game* pthis)
{
    // 设置线条颜色为深棕色（RGB: 139,69,19）
    setlinecolor(RGB(139, 69, 19));
    // 绘制横向网格线，共 ROWS  条
    for (int i = 0; i < ROWS; i++)
    {
        // 从左到右画一条横线
        line(XOFFSET, YOFFSET + i * GRID_SIZE,
            XOFFSET + (COLS -1)* GRID_SIZE, YOFFSET + i * GRID_SIZE);
    }
    // 绘制纵向网格线，共 COLS  条
    for (int j = 0; j <  COLS; j++) 
    {
        // 从上到下画一条竖线
        line(XOFFSET + j * GRID_SIZE, YOFFSET,
            XOFFSET + j * GRID_SIZE, YOFFSET +( ROWS-1) * GRID_SIZE);
    }

    // 遍历整个棋盘，绘制已有棋子
    for (int i = 0; i < ROWS; i++) 
    {
        for (int j = 0; j < COLS; j++) 
        {
            // 如果当前位置有棋子
            if (pthis->map[i][j] != None) 
            {
                // 计算该位置在窗口中的中心坐标
                int x = j * GRID_SIZE + XOFFSET;
                int y = i * GRID_SIZE + YOFFSET;
                // 如果是白棋
                if (pthis->map[i][j] == White)
                {
                    // 设置填充色为白色
                    setfillcolor(WHITE);
                    // 设置边框色为黑色
                    setlinecolor(BLACK);
                }
                else {
                    // 否则是黑棋，设置填充色为黑色
                    setfillcolor(BLACK);
                    // 设置边框色为白色（高亮边缘）
                    setlinecolor(WHITE);
                }
                // 绘制实心圆（半径 15 像素）
                solidcircle(x, y, 15);
            }
        }
    }

    // 如果鼠标悬停在有效位置
    if (pthis->row >= 0 && pthis->col >= 0) 
    {
        // 设置提示圈颜色为蓝色
        setlinecolor(BLUE);
        // 计算悬停位置的屏幕坐标
        int x = pthis->col * GRID_SIZE + XOFFSET;
        int y = pthis->row * GRID_SIZE + YOFFSET;
        // 绘制空心圆作为提示（半径 17 像素）
        circle(x, y, 17);
    }
}