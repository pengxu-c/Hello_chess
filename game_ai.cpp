// game_ai - AI 逻辑实现
// 本文件处理人机对战的 AI 落子策略


#include "game.h"

// 实现 canBlockFive 函数：检测在 (r,c) 落子是否能阻挡人类获胜
bool canBlockFive(Game* pthis, int r, int c)
{
    // 保存原始值
    int old_row = pthis->row;
    int old_col = pthis->col;

    // 设置模拟落子位置
    pthis->map[r][c] = Black;
    pthis->row = r;   //  让 judge 知道“最新落子在这里”
    pthis->col = c;

    bool wouldWin = judge2(pthis, Black);

    // 恢复
    pthis->map[r][c] = None;
    pthis->row = old_row;
    pthis->col = old_col;

    return wouldWin;
}

//以下easyAI实现为简单随机下棋，和应急防输机制
// 实现 aiMoveEasyEasy 函数：简单 AI 落子（简单防输机制）
void aiMoveEasyEasy(Game* pthis) 
{
    // 如果人类步数小于 30，启用防输机制
    if (pthis->humanMoveCount < 30) 
    {
        // 遍历棋盘所有位置
        for (int i = 0; i < ROWS; i++) 
        {
            for (int j = 0; j < COLS; j++) 
            {
                // 如果是空位且能阻挡人类获胜
                if (pthis->map[i][j] == None && canBlockFive(pthis, i, j)) 
                {
                    // AI 立即在此落白棋
                    pthis->map[i][j] = White;
                    // 记录 AI 落子位置，供 judge 使用
                    pthis->row = i;
                    pthis->col = j;
                    // 直接返回，完成落子
                    return;
                }
            }
        }
    }

    // 收集所有空位坐标
    int empty[ROWS * COLS][2];
    // 空位计数器
    int count = 0;
    // 遍历棋盘
    for (int i = 0; i < ROWS; i++) 
    {
        for (int j = 0; j < COLS; j++) 
        {
            // 如果是空位
            if (pthis->map[i][j] == None) 
            {
                // 记录其行列坐标
                empty[count][0] = i;
                empty[count][1] = j;
                // 计数加一
                count++;
            }
        }
    }
    // 如果没有空位，直接返回（棋盘满）
    if (count == 0) return;
    // 随机选择一个空位索引
    int idx = rand() % count;
    // 获取选中的行和列
    int r = empty[idx][0], c = empty[idx][1];
    // AI 在该位置落白棋
    pthis->map[r][c] = White;
    // 记录落子位置
    pthis->row = r;
    pthis->col = c;
}

// 实现 aiMoveEasy 函数：基于防守评分的简单 AI
// 方向扫描思想，仅评估玩家（Black）的威胁，不自身进攻
void aiMoveEasy(Game* pthis)
{
    int bestScore = -1;          // 初始化最高防守得分（-1 表示尚未找到有效空位）
    int bestR = -1, bestC = -1;  // 初始化最佳落子位置
    // 由于分数best初始化为-1，而block初始化为0，所以必然会更新位置

    // 遍历棋盘每一个位置
    for (int i = 0; i < ROWS; i++) 
    {
        for (int j = 0; j < COLS; j++) 
        {
            // 跳过非空位置
            if (pthis->map[i][j] != None) continue;

            // 初始化该位置的防守得分（仅针对人类 Black）
            int blockScore = 0;

            // 定义四个方向：右、下、右下、左下（覆盖横、竖、两斜，避免重复）
            int dr[] = { 0, 1, 1, 1 };
            int dc[] = { 1, 0, 1, -1 };

            // 遍历四个方向，评估人类在此方向上的连续潜力
            for (int d = 0; d < 4; d++)
            {
                int oppCount = 0;  // 统计该方向上人类（Black）的连续子数

                // 正向扫描（+dr[d], +dc[d]）
                for (int step = 1; step <= 5; step++) 
                {
                    int ni = i + step * dr[d];
                    int nj = j + step * dc[d];
                    // 越界则停止
                    if (ni < 0 || ni >= ROWS || nj < 0 || nj >= COLS) break;
                    // 遇到同色（Black）则计数
                    if (pthis->map[ni][nj] == Black) 
                    {
                        oppCount++;
                    }
                    else 
                    {
                        break; // 遇到空位或白子即中断（不考虑跨子连接）
                    }
                }

                // 反向扫描（-dr[d], -dc[d]）
                for (int step = 1; step <= 5; step++)
                {
                    int ni = i - step * dr[d];
                    int nj = j - step * dc[d];
                    if (ni < 0 || ni >= ROWS || nj < 0 || nj >= COLS) break;
                    if (pthis->map[ni][nj] == Black) 
                    {
                        oppCount++;
                    }
                    else 
                    {
                        break;
                    }
                }

                // 根据人类连续子数，赋予防守分值
                if (oppCount >= 6) 
                {
                    blockScore += 1000000; // 禁入级威胁，必须挡
                }
                else if (oppCount >= 5) 
                {
                    blockScore += 40000;  // 四级威胁，必须挡
                }
                else if (oppCount >= 4) 
                {
                    blockScore += 20000;  // 四/五级威胁，必须挡
                }
                else if (oppCount >= 3) 
                {
                    blockScore += 2000;   // 三，高优先级
                }
                else if (oppCount >= 2) 
                {
                    blockScore += 200;    // 成对，中等威胁
                }
                else if (oppCount >= 1) 
                {
                    blockScore += 1;   // 单子，低优先级
                }
               
            }

            // 如果当前空位的防守价值更高，则更新最佳位置
            if (blockScore > bestScore) {
                bestScore = blockScore;
                bestR = i;         
                bestC = j;
            }
        }
    }

    // 如果找到了有效落子位置（理论上只要棋盘未满就一定有）
    if (bestR != -1) 
    {
        pthis->map[bestR][bestC] = White;  // AI 落白子
        pthis->row = bestR;                // 记录位置供 judge 使用
        pthis->col = bestC;
    }
    // 注：若棋盘已满（bestR == -1），游戏应已结束，无需处理
}

// 实现 aiMoveHard 函数：困难 AI 落子（基于攻防评分）
void aiMoveHard(Game* pthis)
{
    
    // 初始化最高分数为 -1
    int bestScore = -1;
    // 初始化最佳落子位置为无效值
    int bestR = -1, bestC = -1;
    // 遍历棋盘每个位置
    for (int i = 0; i < ROWS; i++) 
    {
        for (int j = 0; j < COLS; j++)
        {
            int diew[4] = {0};
            int dieb[4] = {0};  //判断是否为活棋
            // 跳过非空位置
            if (pthis->map[i][j] != None) continue;
            // 初始化自身进攻得分和防守阻拦得分
            int selfScore = 0, blockScore = 0;
            // 定义四个方向
            int dr[] = { 0, 1, 1, 1 };
            int dc[] = { 1, 0, 1, -1 };
            // 遍历四个方向
            for (int d = 0; d < 4; d++) 
            {
                // 统计该方向上 AI（白棋）的连续子数
                int selfCount = 0;
                // 向正方向统计
                for (int step = 1; step <= 5; step++)
                {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (ni < 0 || ni >= ROWS || nj < 0 || nj >= COLS) break;
                    if (pthis->map[ni][nj] == White) selfCount++;
                    else if (pthis->map[ni][nj] == Black) 
                    {
                        diew[d]++;
                        break;
                    }
                    else break;
                }
                // 向反方向统计
                for (int step = 1; step <= 5; step++) 
                {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (ni < 0 || ni >= ROWS || nj < 0 || nj >= COLS) break;
                    if (pthis->map[ni][nj] == White) selfCount++;
                    else if (pthis->map[ni][nj] == Black)
                    {
                        diew[d]++;
                        break;
                    }
                    else break;
                }
                // 统计该方向上人类（黑棋）的连续子数
                int oppCount = 0;
                // 向正方向统计
                for (int step = 1; step <= 5; step++) 
                {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (ni < 0 || ni >= ROWS || nj < 0 || nj >= COLS) break;
                    if (pthis->map[ni][nj] == Black) oppCount++;
                    else if (pthis->map[ni][nj] == White)
                    {
                        dieb[d]++;
                        break;
                    }
                    else break;
                }
                // 向反方向统计
                for (int step = 1; step <= 5; step++) 
                {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (ni < 0 || ni >= ROWS || nj < 0 || nj >= COLS) break;
                    if (pthis->map[ni][nj] == Black) oppCount++;
                    else if (pthis->map[ni][nj] == White)
                    {
                        dieb[d]++;
                        break;
                    }
                    else break;
                }

                // 根据 AI 连续子数加分（鼓励进攻）
                if (selfCount >= 6) selfScore += 1000000;
                else if (selfCount == 5) 
                {
                    // 如果两端都被堵了(diew[d] >= 2)，就是死五，分数大幅降低
                    if (diew[d] >= 2) selfScore += 100; // 死五
                    else selfScore += 40000;             // 活五或半活五
                }
                else if (selfCount == 4) 
                {
                    if (diew[d] >= 2) selfScore += 30;  // 死四
                    else selfScore += 10000;             // 活四或半活四
                }
                else if (selfCount == 3) 
                {
                    if (diew[d] >= 2) selfScore += 20;   // 死三
                    else selfScore += 1000;              // 活三或半活三
                }
                else if (selfCount >= 2) 
                {
                    if (diew[d] >= 2) selfScore += 5;    // 死二
                    else selfScore += 100;               // 活二或半活二
                }

                // 根据人类连续子数加分（优先防守）
                if (oppCount >= 6) blockScore += 50000;
                else if (oppCount >= 5) 
                {
                    // 如果两端都被堵了(dieb[d] >= 2)，就是死五，威胁降低
                    if (dieb[d] >= 2) blockScore += 100; // 死五
                    else blockScore += 30000;             // 活五或半活五，必须挡
                }
                else if (oppCount >= 4) 
                {
                    if (dieb[d] >= 2) blockScore += 80;  // 死四
                    else blockScore += 20000;             // 活四或半活四，高威胁
                }
                else if (oppCount >= 3) 
                {
                    if (dieb[d] >= 2) blockScore += 10;   // 死三
                    else blockScore += 2000;              // 活三或半活三
                }
                else if (oppCount >= 2) 
                {
                    if (dieb[d] >= 2) blockScore += 1;    // 死二
                    else blockScore += 200;               // 活二或半活二
                }
                else if (oppCount >= 1) 
                {
                    blockScore += 1;
                }
                /*    已经使用加强版
                // 根据 AI 连续子数加分（鼓励进攻）
                if (selfCount>=6) selfScore += 1000000; // 五，最高优先级（进攻得分更高才能使总分超过活五得分
                if (selfCount ==5) selfScore += 40000;// 五，高优先级
                if (selfCount == 4) selfScore += 10000;
                if (selfCount == 3) selfScore += 1000;
                else if (selfCount >= 2) selfScore += 100;
                // 根据人类连续子数加分（优先防守）
                if (oppCount>=6) blockScore += 50000; // 死五，高优先级（防守阻拦得分更高才能使总分超过活五得分
                else if (oppCount >= 5) blockScore += 30000;
                else if (oppCount >= 4) blockScore += 20000;
                else if (oppCount >= 3) blockScore += 2000;
                else if (oppCount >= 2) blockScore += 200;
                else if (oppCount >= 1) blockScore += 1;//孤立子，但也要加分，防止AI无脑乱下！！
                */
            }
            // 计算总分
            int total = selfScore + blockScore;
            // 如果总分更高，更新最佳位置
            if (total > bestScore) 
            {
                bestScore = total;
                bestR = i;
                bestC = j;
            }
        }
    }
    // 如果找到了最佳位置
    if (bestR != -1) 
    {
        // AI 落子
        pthis->map[bestR][bestC] = White;
        // 记录位置
        pthis->row = bestR;
        pthis->col = bestC;
    }
    else 
    {
        // 否则退化为简单 AI（理论上不会发生）
        //以防万一，兜底逻辑，保证游戏可以继续进行
        aiMoveEasy(pthis);
    }
}