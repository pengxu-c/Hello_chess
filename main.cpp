//目前处于冗余注释阶段。

// main.c - 程序主入口
// 本文件包含程序执行的主流程控制


#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <io.h>


int main() 
{
    bool playAgain = true;

    while (playAgain) 
    {
        // 调用 selectMode 函数，获取用户选择的游戏模式。
        GameMode mode = selectMode();

        // 定义游戏对象 game，类型为 struct Game
        Game game;

        // 调用 init 函数，初始化图形窗口（960x600）和游戏状态
        init(&game, 960, 600, mode);

        // 清空控制台，并显示初始提示
        system("cls");
        printf("当前黑棋步数：0\n");

        // 定义 IMAGE 类型变量 img_bg，用于加载背景图片
        IMAGE img_bg;

        // 定义布尔变量 hasBackground，标记背景图是否存在
        bool hasBackground = false;

        // 使用 _access 函数检测文件 "Resource/images/bk.jpg" 是否存在
        if (_access("Resource/images/bk.jpg", 0) == 0) 
        {
            // 如果存在，调用 loadimage 加载背景图片
            loadimage(&img_bg, L"Resource/images/bk.jpg");
            // 设置 hasBackground 为 true
            hasBackground = true;
        }

        // 进入游戏主循环，只要 isRunning 为 true 就持续运行
        while (game.isRunning) 
        {
            // 判断是否为人机模式且当前不是人类回合（即轮到 AI）
            if ((game.mode == PVC_EASY || game.mode == PVC_HARD|| game.mode == PVC_EASIER) && !game.isHumanTurn) {
                // 暂停500 毫秒，便于玩家观察 AI 落子过程。
                Sleep(500);

                // 根据游戏模式调用对应的 AI 落子函数
                if (game.mode == PVC_EASY) 
                {
                    aiMoveEasy(&game);
                }
                else if (game.mode == PVC_EASIER)
                {
                     aiMoveEasyEasy(&game);   //超简单逻辑
                }
                else 
                {
                    aiMoveHard(&game);
                }

                // 调用 judge 函数，检查 AI（白棋）是否获胜
                if (judge(&game, White)) 
                {
                    // 弹出消息框提示“AI 获胜”
                    MessageBox(GetHWnd(), L"AI 获胜！", L"游戏结束", MB_OK);
                    // 设置游戏结束标志
                    game.isRunning = false;
                    // 跳出循环
                    break;
                }
                // 检查 AI 落子后是否平局
                else if (isBoardFull(&game)) 
                {  
                    MessageBox(GetHWnd(), L"棋盘已满，平局！", L"游戏结束", MB_OK | MB_ICONINFORMATION);
                    game.isRunning = false;
                    break;
                }

                // 将回合交还给人类玩家
                game.isHumanTurn = true;
                // 跳过本次循环剩余部分，直接进入下一帧
                continue;
            }

            // 检查是否有鼠标消息到达
            if (peekmessage(&game.msg, EX_MOUSE))
            {
                // 重置 hasMovedThisClick 标志，表示本次点击尚未落子
                game.hasMovedThisClick = false;
                // 调用 update 函数处理鼠标事件
                update(&game);

                // 如果是人机模式且发生了鼠标左键点击
                if (game.mode != PVP && game.msg.message == WM_LBUTTONDOWN) 
                {
                    // 检查本次点击是否成功落子
                    if (game.hasMovedThisClick) 
                    {
                        // 人类成功落子，步数已在 update 中增加并打印

                        // 检查人类（黑棋）是否获胜
                        if (judge(&game, Black)) 
                        {
                            // 弹出胜利提示
                            MessageBox(GetHWnd(), L"恭喜你，战胜了 AI！", L"游戏结束", MB_OK);
                            // 结束游戏
                            game.isRunning = false;
                            // 跳出循环
                            break;
                        }
                        // 人类落子后平局检测
                        else if (isBoardFull(&game))
                        {
                            MessageBox(GetHWnd(), L"棋盘已满，平局！", L"游戏结束", MB_OK | MB_ICONINFORMATION);
                            game.isRunning = false;
                            break;
                        }
                        // 交出回合给 AI
                        game.isHumanTurn = false;
                    }
                }
            }

            // 如果是双人模式且发生了鼠标左键点击
            if (game.mode == PVP && game.msg.message == WM_LBUTTONDOWN)
            {
                // 检查是否成功落子
                if (game.hasMovedThisClick)
                {
                    // 获胜者是刚刚落子的一方（即切换前的 currentChessType 的反方）
                    ChessType winner = (ChessType)(-game.currentChessType);
                    // 判断该方是否获胜
                    if (judge(&game, winner))
                    {
                        // 定义宽字符数组用于存储提示信息
                        wchar_t msg[100];
                        // 根据获胜方设置提示文本
                        if (winner == Black)
                            wcscpy_s(msg, L"黑棋获胜！");
                        else
                            wcscpy_s(msg, L"白棋获胜！");
                        // 弹出胜负提示框
                        MessageBox(GetHWnd(), msg, L"游戏结束", MB_OK);
                        // 结束游戏
                        game.isRunning = false;
                        // 跳出循环
                        break;
                    }
                    // 落子后平局检测
                    else if (isBoardFull(&game))
                    {
                        MessageBox(GetHWnd(), L"棋盘已满，平局！", L"游戏结束", MB_OK | MB_ICONINFORMATION);
                        game.isRunning = false;
                        break;
                    }
                }
            }

            // 开始批量绘图，提升渲染性能
            BeginBatchDraw();
            // 如果存在背景图
            if (hasBackground)
            {
                // 绘制背景图到窗口左上角
                putimage(0, 0, &img_bg);
            }
            else 
            {
                // 否则设置背景色为浅棕色
                setbkcolor(RGB(240, 220, 180));
                // 清空整个窗口
                cleardevice();
            }
            // 调用 render 函数绘制棋盘和棋子
            render(&game);
            // 结束批量绘图并刷新屏幕
            EndBatchDraw();
        }

        // 游戏结束后询问是否再玩一次
        int result = MessageBox(GetHWnd(), L"是否再来一局？", L"游戏结束", MB_YESNO | MB_ICONQUESTION);
        playAgain = (result == IDYES);
    }

    // 关闭图形窗口，释放资源
    closegraph();
    
    return 0;
}