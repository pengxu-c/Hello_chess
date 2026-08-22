// ============================================================
// controller.h - 游戏控制器类声明
// 组合 Board/Judge/Stats/UI/Player/StorageManager，主导游戏流程
// ============================================================
#pragma once
#include "core.h"
#include "storage.h"
#include "ai_config.h"

class UI;
class Player;

class GameController {
public:
    GameController();
    ~GameController();
    void run();                 // 主入口：选玩家→开窗→循环对局
private:
    int boardSize_ = 15;        // 棋盘尺寸 N（N×N）
    int winLength_ = 5;         // 连珠获胜数 n
    int p1Type_ = 1;            // 玩家1类型编号
    int p2Type_ = 1;            // 玩家2类型编号
    Board board_;
    Judge judge_;
    Stats stats_;
    UI* ui_ = nullptr;
    Player* player1_ = nullptr; // 执黑先手
    Player* player2_ = nullptr; // 执白后手
    StorageManager storage_;    // 存储管理器
    AIConfig aiConfig_;          // API AI 配置

    Player* createPlayer(int choice);   // 按编号创建棋手
    void recreatePlayers(int p1Type, int p2Type);  // 释放旧玩家并按编号创建新玩家
    bool askReturnToMenu();             // 询问是否返回主菜单，返回 true=是 false=否/输入失败
    void configureRules();              // 终端配置棋盘尺寸与连珠数 + 存储开关
    void selectPlayers();               // 终端选择双方棋手
    void playOneGame();                 // 进行一局对战
    void applyBoardLayout();            // 按 boardSize_ 计算网格像素与偏移
    bool loadResumeMenu();              // 拼棋载入菜单
    void replayMenu();                  // 棋局回访菜单
};
