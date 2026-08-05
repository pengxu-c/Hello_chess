// game_judge - 胜负判定实现
// 本文件处理六子连珠的判定逻辑


#include "game.h"

// 实现 checkLine 函数：检查从 (r,c) 沿 (dr,dc) 方向是否有连续 6 个指定棋子
bool checkLine(Game* pthis, int r, int c, int dr, int dc, int chess)
{
    // 如果起始点越界，直接返回 false
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return false;
    // 循环检查接下来的 6 个位置
    for (int i = 0; i < 6; i++) 
    {
        // 计算第 i 个位置的坐标
        int nr = r + i * dr;
        int nc = c + i * dc;
        // 如果越界，返回 false
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return false;
        // 如果该位置棋子不等于目标棋子，返回 false
        if (pthis->map[nr][nc] != chess) return false;
    }
    // 全部匹配，返回 true
    return true;
}

// 实现 judge 函数：判断指定颜色是否形成六子连珠
bool judge(Game* pthis, ChessType color) 
{
    // 如果上次落子位置无效，直接返回 false
    if (pthis->row == -1 || pthis->col == -1) return false;
    // 获取上次落子的行列坐标
    int r = pthis->row, c = pthis->col;
    // 定义四个方向：右、下、右下、左下
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    // 遍历四个方向
    for (int d = 0; d < 4; d++)
    {
        // 对每个方向，尝试以当前落子点为第 0 到第 5 个位置
        for (int offset = 0; offset < 6; offset++) 
        {
            // 计算可能的六连起点
            int start_r = r - offset * dr[d];
            int start_c = c - offset * dc[d];
            // 调用 checkLine 检查该起点是否形成六连
            if (checkLine(pthis, start_r, start_c, dr[d], dc[d], (int)color))
            {
                // 如果形成，返回 true
                return true;
            }
        }
    }
    // 所有方向均未形成六连，返回 false
    return false;
}

// 函数判断棋盘是否已满
bool isBoardFull(Game* pthis) 
{
    for (int i = 0; i < ROWS; i++) 
    {
        for (int j = 0; j < COLS; j++) 
        {
            if (pthis->map[i][j] == None) 
            {
                return false;
            }
        }
    }
    return true;
}

//专门给简单模式的判断函数.
// 先加一条五连辅助函数，原来 6 现在 5
static bool checkLine5(Game* pthis, int r, int c, int dr, int dc, int chess)
{
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return false;
    for (int i = 0; i < 5; i++)          // 这里原来是 6
    {
        int nr = r + i * dr;
        int nc = c + i * dc;
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return false;
        if (pthis->map[nr][nc] != chess) return false;
    }
    return true;
}

// 五连判定入口， judge 写法
bool judge2(Game* pthis, ChessType color)
{
    if (pthis->row == -1 || pthis->col == -1) return false;
    int r = pthis->row, c = pthis->col;
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };

    for (int d = 0; d < 4; d++)
    {
        for (int offset = 0; offset < 5; offset++)   // 这里原来是 6
        {
            int start_r = r - offset * dr[d];
            int start_c = c - offset * dc[d];
            if (checkLine5(pthis, start_r, start_c, dr[d], dc[d], (int)color))
                return true;
        }
    }
    return false;
}