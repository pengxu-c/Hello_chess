// ============================================================
// controller.h - 游戏控制器类声明
// 组合 Board/Judge/Stats/UI/Player，主导游戏流程
// ============================================================
#pragma once
#include "core.h"

class UI;
class Player;

class GameController {
public:
    GameController();
    ~GameController();
    void run();                 // 主入口：选玩家→开窗→循环对局
private:
    Board board_;
    Judge judge_;
    Stats stats_;
    UI* ui_ = nullptr;
    Player* player1_ = nullptr; // 执黑先手
    Player* player2_ = nullptr; // 执白后手
    Player* createPlayer(int choice);   // 按编号创建棋手
    void selectPlayers();               // 终端选择双方棋手
    void playOneGame();                 // 进行一局对战
};
