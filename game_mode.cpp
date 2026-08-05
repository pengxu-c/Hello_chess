// game_mode - 游戏模式选择功能实现
// 本文件负责在程序启动时，通过控制台提示用户选择游戏模式。
// 支持四种模式：双人对战、人机简单、人机超简单、人机困难。
// 函数返回值为 enum GameMode 类型，供主程序初始化使用。
// 此模块独立于图形界面，仅依赖标准输入输出。

#include "game.h"      //  GameMode 枚举定义和相关类型
#include <stdio.h>     

// 函数：selectMode
// 功能：在控制台中显示游戏模式选项，等待用户输入并返回对应的游戏模式
// 返回值：GameMode 枚举值
// 注意：包含基本的输入合法性校验，防止非数字输入导致程序异常
GameMode selectMode()
{
    int choice;  // 用于存储用户输入的选项编号
    // 向控制台打印游戏简介信息
    printf("【六子棋规则简介】\n");
    printf("1. 棋盘：15×15 的网格。\n");
    printf("2. 玩家：黑棋先手，双方轮流落子。\n");
    printf("3. 胜利条件：率先在横、竖、斜方向形成连续 6 颗同色棋子（六连）者获胜。\n");
    printf("4. 注意：五连不胜，必须恰好六连或以上才算赢。\n");
    printf("    游戏愉快！\n\n\n\n");
    // 向控制台打印游戏模式选择菜单
    printf("请选择游戏模式：\n");
    printf("1. 双人对战 ——(PVP)\n");
    printf("2. 人机对战 —— 易如反掌（超简单）\n");
    printf("3. 人机对战 —— 长线思考（更好玩）\n");
    printf("4. 人机对战 —— 深度推理（最智能）\n");
    printf("请输入选项 (1-4): \n如果是无效选项，默认进入双人对战模式。\n");

    // 循环读取用户输入，直到获得一个有效的整数
    // scanf 返回成功读取的项数，若输入非整数（如字母），返回值不为 1
    while (scanf_s("%d", &choice) != 1) {
        // 提示用户输入无效
        printf("输入无效，请输入数字 1~4: ");
        // 清空输入缓冲区中的非法字符（如 "abc\n"），避免无限循环
        // getchar() 逐个读取字符，直到遇到换行符 '\n'
        while (getchar() != '\n');
    }

    // 根据用户输入的数字，返回对应的 GameMode 枚举值
    switch (choice) 
    {
    case 1:
        return PVP;         // 双人对战模式
    case 3:
        return PVC_EASY;    // 人机简单模式（AI 防输机制）
    case 2: 
        return PVC_EASIER;  //超简单模式。
    case 4:
        return PVC_HARD;    // 人机困难模式（AI 具备攻防评分策略）
    case 111111:
        printf("敬请期待"); 
        Sleep(10000);
        break;
    default:
        // 如果输入超出 1~4 范围，默认进入双人对战模式
        printf("无效选项，默认进入双人对战模式。\n");
        return PVP;
    }
}