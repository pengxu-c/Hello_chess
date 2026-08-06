// ============================================================
// controller.cpp - GameController 实现
// 主导：选择棋手 → 初始化窗口 → 循环对局 → 询问重开
// ============================================================
#include "controller.h"
#include "ui.h"
#include "player.h"
#include <cstdio>
#include <cstdlib>
#include <windows.h>

GameController::GameController() {
    ui_ = new UI();
}

GameController::~GameController() {
    delete player1_;
    delete player2_;
    delete ui_;
}

// 按编号创建棋手对象
Player* GameController::createPlayer(int choice) {
    switch (choice) {
        case 1: return new HumanPlayer(*ui_);
        case 2: return new EasyJudgeAI(judge_, stats_);
        case 3: return new PureGreed10();
        case 4: return new PureGreed11();
        default: return new HumanPlayer(*ui_);
    }
}

// 终端选择双方棋手类型
void GameController::selectPlayers() {
    printf("[Six-in-a-row Rules]\n");
    printf("1. Board: 15x15 grid.\n");
    printf("2. Players: Black moves first, turns alternate.\n");
    printf("3. Win: first to form 6 in a row (horizontal/vertical/diagonal) wins.\n");
    printf("4. Note: five in a row does NOT win; exactly six or more is required.\n");
    printf("    Enjoy!\n\n");

    printf("Available player types:\n");
    printf("  1. Human\n");
    printf("  2. EasyJudge (super easy: random + block)\n");
    printf("  3. PureGreed 1.0 (defense-only scoring)\n");
    printf("  4. PureGreed 1.1 (attack+defense scoring)\n\n");

    int c1 = 0, c2 = 0;
    printf("Choose player 1 (Black, first) type number: ");
    if (scanf_s("%d", &c1) != 1) c1 = 1;
    printf("Choose player 2 (White, second) type number: ");
    if (scanf_s("%d", &c2) != 1) c2 = 1;

    delete player1_;
    delete player2_;
    player1_ = createPlayer(c1);
    player2_ = createPlayer(c2);

    printf("\nPlayer 1: %s (Black)  vs  Player 2: %s (White)\n", player1_->name(), player2_->name());
    printf("----------------------------------\n");
}

// 进行一局对战
void GameController::playOneGame() {
    board_.clear();
    stats_.reset();
    printf("Current black move count: 0\n");

    ChessType currentColor = ChessType::Black;   // 黑棋先手
    Player* current = player1_;
    bool running = true;

    while (running) {
        // AI 回合延迟，便于观察
        if (!current->isHuman()) Sleep(500);

        ui_->pollMouse();
        Pos pos = current->place(board_, currentColor);

        if (pos.valid() && board_.inBounds(pos.r, pos.c)) {
            if (board_.at(pos.r, pos.c) != ChessType::None) {
                // 该位置已有棋子
                if (current->isHuman()) {
                    ui_->messageBox(L"此处已有棋子，请选择空位！");
                }
            } else {
                // 落子
                board_.place(pos.r, pos.c, currentColor);
                stats_.recordMove(currentColor);
                if (currentColor == ChessType::Black) stats_.printProgress();

                // 判定胜负
                if (judge_.checkWin(board_, pos, currentColor, 6)) {
                    const wchar_t* who = (current == player1_) ? L"玩家1 获胜！" : L"玩家2 获胜！";
                    ui_->messageBox(who);
                    running = false;
                } else if (board_.isFull()) {
                    ui_->messageBox(L"棋盘已满，平局！");
                    running = false;
                } else {
                    // 切换回合
                    currentColor = opponent(currentColor);
                    current = (current == player1_) ? player2_ : player1_;
                }
            }
        }

        ui_->render(board_, ui_->hoverPos());
    }
}

// 主入口
void GameController::run() {
    selectPlayers();
    ui_->initWindow(960, 600);
    ui_->loadBackground("Resource/images/bk.jpg");
    system("cls");

    bool playAgain = true;
    while (playAgain) {
        playOneGame();
        playAgain = (ui_->askYesNo(L"是否再来一局？") == IDYES);
    }

    ui_->close();
}
