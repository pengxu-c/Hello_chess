// ============================================================
// controller.cpp - GameController 实现
// 主导：选择棋手 → 初始化窗口 → 循环对局 → 询问重开
// 集成 StorageManager：记忆存储开关、悔棋、回访、残局、统计、控制台命令
// ============================================================
#include "controller.h"
#include "ui.h"
#include "player.h"
#include "ai_player.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <ctime>
#include <conio.h>
#include <string>

GameController::GameController() {
    ui_ = new UI();
    srand((unsigned int)time(NULL));
    aiConfig_.loadFromFile();
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
        case 2: return new GreedyScoringAI(0.0, "EasyJudge");         // 纯防守
        case 3: return new GreedyScoringAI(0.0, "PureGreed 1.0");     // 纯防守
        case 4: return new GreedyScoringAI(0.9, "PureGreed 1.1");     // 攻防
        case 5: return new MinimaxPP(judge_);
        case 6:
            // API 未配置或配置不完整时，自动回退到 Minimax++（玩家5）
            if (aiConfig_.enabled) return new APIPlayer(aiConfig_);
            printf(">> API AI unavailable (config.json missing or incomplete). Falling back to Minimax++.\n");
            return new MinimaxPP(judge_);
        default: return new HumanPlayer(*ui_);
    }
}

// 按 boardSize_ 计算网格像素与偏移，使棋盘居中 960×600 窗口
void GameController::applyBoardLayout() {
    GRID_SIZE = 560 / (boardSize_ - 1);
    if (GRID_SIZE < 12) GRID_SIZE = 12;
    XOFFSET = (960 - (boardSize_ - 1) * GRID_SIZE) / 2;
    YOFFSET = (600 - (boardSize_ - 1) * GRID_SIZE) / 2;
}

// 终端配置棋盘尺寸与连珠数 + 存储开关
void GameController::configureRules() {
    printf("Customize rules? Type 'c' to customize, or press Enter for default (15x15, 5-in-a-row): ");
    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return;
    if (buf[0] == 'c' || buf[0] == 'C') {
        int n = 15, k = 5;
        printf("Board size N (5..30, default 15): ");
        if (fgets(buf, sizeof(buf), stdin) && sscanf_s(buf, "%d", &n) != 1) n = 15;
        if (n < 5 || n > 30) n = 15;
        printf("Win length k (3..%d, default 5): ", n);
        if (fgets(buf, sizeof(buf), stdin) && sscanf_s(buf, "%d", &k) != 1) k = 5;
        if (k < 3 || k > n) k = 5;
        boardSize_ = n;
        winLength_ = k;
    }

    ROWS = COLS = boardSize_;
    WIN_LEN = winLength_;
    applyBoardLayout();
    board_.resize();
    printf("Rules set: %dx%d board, %d-in-a-row to win.\n\n", boardSize_, boardSize_, winLength_);

    // 记忆存储开关（默认关闭）
    printf("Enable memory storage? (y/n, default n): ");
    char buf2[64];
    storage_.setEnabled(false);
    if (fgets(buf2, sizeof(buf2), stdin)) {
        storage_.setEnabled(buf2[0] == 'y' || buf2[0] == 'Y');
    }
    if (storage_.isEnabled()) {
        printf(">> Memory storage ENABLED. Data saved to '%s\\' directory.\n",
               storage_.config().dataDir.c_str());
        printf(">> In-game commands (type in console): help, save, undo [n], abort, stats\n\n");
    } else {
        printf(">> Memory storage disabled.\n\n");
    }
}

// 终端选择双方棋手类型
void GameController::selectPlayers() {
    configureRules();

    printf("[Game Rules]\n");
    printf("1. Board: %dx%d grid.\n", boardSize_, boardSize_);
    printf("2. Players: Black moves first, turns alternate.\n");
    printf("3. Win: first to form %d in a row (horizontal/vertical/diagonal) wins.\n", winLength_);
    printf("    Enjoy!\n\n");

    printf("Available player types:\n");
    printf("  1. Human\n");
    printf("  2. EasyJudge (super easy: random + block)\n");
    printf("  3. PureGreed 1.0 (defense-only scoring)\n");
    printf("  4. PureGreed 1.1 (attack+defense scoring)\n");
    printf("  5. Minimax++ (alpha-beta search)\n");
    if (aiConfig_.enabled)
        printf("  6. API AI - %s (remote LLM)\n", aiConfig_.displayName.c_str());
    else
        printf("  6. API AI (unavailable; falls back to Minimax++)\n");
    printf("\n");

    int c1 = 0, c2 = 0;
    char buf2[64];
    printf("Choose player 1 (Black, first) type number: ");
    if (!fgets(buf2, sizeof(buf2), stdin) || sscanf_s(buf2, "%d", &c1) != 1) c1 = 1;
    printf("Choose player 2 (White, second) type number: ");
    if (!fgets(buf2, sizeof(buf2), stdin) || sscanf_s(buf2, "%d", &c2) != 1) c2 = 1;

    p1Type_ = c1;
    p2Type_ = c2;
    delete player1_;
    delete player2_;
    player1_ = createPlayer(c1);
    player2_ = createPlayer(c2);

    printf("\nPlayer 1: %s (Black)  vs  Player 2: %s (White)\n", player1_->name(), player2_->name());
    printf("----------------------------------\n");
}

// 残局载入菜单：返回是否成功载入
bool GameController::loadResumeMenu() {
    auto ids = storage_.listResumes();
    if (ids.empty()) {
        printf("No saved resumes found.\n");
        return false;
    }
    printf("=== Saved Resumes ===\n");
    for (const auto& id : ids) {
        GameRecord r;
        if (storage_.loadResume(id, r)) {
            printf("  %s | %dx%d | %s vs %s | moves=%d | note=%s\n",
                   id.c_str(), r.boardSize, r.winLength,
                   r.player1Name.c_str(), r.player2Name.c_str(),
                   (int)r.moves.size(), r.note.c_str());
        }
    }
    printf("Enter resume id to load (or press Enter to cancel): ");
    char idbuf[128];
    if (!fgets(idbuf, sizeof(idbuf), stdin)) return false;
    std::string id = idbuf;
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r')) id.pop_back();
    if (id.empty()) return false;

    int bs = 15, wl = 5, p1t = 1, p2t = 1;
    if (!storage_.restoreResume(id, board_, bs, wl, p1t, p2t)) {
        printf("Failed to load resume: %s\n", id.c_str());
        return false;
    }
    boardSize_ = bs;
    winLength_ = wl;
    p1Type_ = p1t;
    p2Type_ = p2t;
    applyBoardLayout();
    delete player1_;
    delete player2_;
    player1_ = createPlayer(p1t);
    player2_ = createPlayer(p2t);
    printf(">> Resume loaded: %s (%dx%d, %s vs %s, %d moves)\n",
           id.c_str(), bs, wl, player1_->name(), player2_->name(),
           storage_.moveCount());
    return true;
}

// 棋局回访菜单：逐步回放
void GameController::replayMenu() {
    auto ids = storage_.listGames();
    if (ids.empty()) {
        printf("No saved games found.\n");
        return;
    }
    printf("=== Saved Games ===\n");
    for (const auto& id : ids) {
        GameRecord r;
        if (storage_.loadGame(id, r)) {
            printf("  %s | %dx%d | %s vs %s | moves=%d\n",
                   id.c_str(), r.boardSize, r.winLength,
                   r.player1Name.c_str(), r.player2Name.c_str(),
                   (int)r.moves.size());
        }
    }
    printf("Enter game id to replay (or press Enter to cancel): ");
    char idbuf[128];
    if (!fgets(idbuf, sizeof(idbuf), stdin)) return;
    std::string id = idbuf;
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r')) id.pop_back();
    if (id.empty()) return;

    GameRecord record;
    if (!storage_.loadGame(id, record)) {
        printf("Game not found: %s\n", id.c_str());
        return;
    }

    boardSize_ = record.boardSize;
    winLength_ = record.winLength;
    ROWS = COLS = boardSize_;
    WIN_LEN = winLength_;
    applyBoardLayout();
    board_.resize();

    ui_->initWindow(960, 600);


    int step = 0;
    int total = (int)record.moves.size();
    printf("Replaying %s: %d moves. [Space]=next  [B]=prev  [Home]=first  [End]=last  [ESC]=exit\n",
           id.c_str(), total);

    while (true) {
        storage_.replayGame(id, board_, step);
        ui_->render(board_, { -1, -1 });

        printf("\rStep %d / %d  ", step, total);

        if (_kbhit()) {
            int ch = _getch();
            if (ch == 27) break;                          // ESC
            else if (ch == ' ' && step < total) step++;   // Space
            else if ((ch == 'b' || ch == 'B') && step > 0) step--;  // B
            else if (ch == 0 || ch == 224) {              // 特殊键
                if (_kbhit()) {
                    int ext = _getch();
                    if (ext == 71 && step > 0) step = 0;           // Home
                    else if (ext == 79 && step < total) step = total; // End
                }
            }
        }
        Sleep(30);
    }
    printf("\nReplay ended.\n");
    ui_->close();
}

// 进行一局对战
void GameController::playOneGame() {
    board_.clear();
    stats_.reset();

    // 开始记录棋局
    storage_.startGame(boardSize_, winLength_,
                       player1_->name(), player2_->name(), p1Type_, p2Type_);

    ChessType currentColor = ChessType::Black;   // 黑棋先手
    Player* current = player1_;
    bool running = true;

    while (running) {
        // 非阻塞检测控制台命令
        if (storage_.isEnabled()) {
            std::string cmd = StorageManager::tryReadConsoleLine();
            if (!cmd.empty()) {
                storage_.handleConsoleCommand(cmd, board_);
                if (storage_.isAbortRequested()) {
                    storage_.clearAbortRequest();
                    storage_.endGame(GameStatus::Aborted);
                    running = false;
                    break;
                }
            }
        }

        // AI 回合延迟，便于观察
        if (current->needsDelay()) Sleep(500);

        ui_->pollMouse();
        Pos pos = current->place(board_, currentColor);

        if (pos.valid() && board_.inBounds(pos.r, pos.c)) {
            if (board_.at(pos.r, pos.c) != ChessType::None) {
                // 该位置已有棋子
                if (current->isHuman()) {
                    ui_->messageBox(L"Occupied! Choose an empty cell.");
                }
            } else {
                // 落子
                board_.place(pos.r, pos.c, currentColor);
                current->markLastMove(pos);          // 记录该玩家最后一手（供 UI 闪烁标记）
                stats_.recordMove(currentColor);
                storage_.recordMove(pos.r, pos.c, currentColor);

                // 判定胜负
                if (judge_.checkWin(board_, pos, currentColor, winLength_)) {
                    const wchar_t* who = (current == player1_) ? L"Player 1 wins!" : L"Player 2 wins!";
                    ui_->messageBox(who);
                    storage_.endGame(current == player1_ ? GameStatus::BlackWin : GameStatus::WhiteWin);
                    running = false;
                } else if (board_.isFull()) {
                    ui_->messageBox(L"Board full! Draw!");
                    storage_.endGame(GameStatus::Draw);
                    running = false;
                } else {
                    // 切换回合：颜色取反，玩家指针交换
                    currentColor = opponent(currentColor);
                    current = (current == player1_) ? player2_ : player1_;
                }
            }
        }

        ui_->render(board_, ui_->hoverPos(),      // 传入双方最后一手标记 + 当前回合方
                    player1_->lastMove(), player2_->lastMove(), currentColor);
    }

    // 若游戏仍进行中（如窗口关闭），保存为残局
    if (storage_.isEnabled() && storage_.isInGame()) {
        storage_.saveResume("Auto-saved on exit");
        storage_.endGame(GameStatus::Aborted);
    }
}

// 主入口：外层循环允许结束后回到主菜单重新选择棋手
void GameController::run() {
    while (true) {
        selectPlayers();

        // 存储启用时提供额外选项
        bool resumeLoaded = false;
        if (storage_.isEnabled()) {
            printf("\n=== Storage Options ===\n");
            printf("  1. New game\n");
            printf("  2. Load resume (continue saved game)\n");
            printf("  3. Replay saved game\n");
            printf("  4. Show global statistics\n");
            printf("Choice (default 1): ");
            char buf[16];
            int sc = 1;
            if (fgets(buf, sizeof(buf), stdin)) {
                sscanf_s(buf, "%d", &sc);
            }
            if (sc == 2) {
                resumeLoaded = loadResumeMenu();
            } else if (sc == 3) {
                replayMenu();
                printf("Return to main menu? (y/n): ");
                char b2[16];
                if (!fgets(b2, sizeof(b2), stdin)) break;
                if (b2[0] != 'y' && b2[0] != 'Y') break;
                continue;
            } else if (sc == 4) {
                storage_.printGlobalStats();
                printf("Return to main menu? (y/n): ");
                char b2[16];
                if (!fgets(b2, sizeof(b2), stdin)) break;
                if (b2[0] != 'y' && b2[0] != 'Y') break;
                continue;
            }
        }

        ui_->initWindow(960, 600);


        if (resumeLoaded) {
            // 残局已恢复到 board_，渲染初始状态
            ui_->render(board_, { -1, -1 });
        }

        // 同一棋手组合下可连续对局
        bool playAgain = true;
        while (playAgain) {
            playOneGame();
            playAgain = (ui_->askYesNo(L"Play again?") == IDYES);
            if (playAgain) {
                // 再来一局时清空棋盘
                board_.clear();
            }
        }

        ui_->close();

        // 窗口已关，控制台询问是否回到主菜单重新选择棋手
        printf("Return to main menu? (y/n): ");
        char buf[16];
        if (!fgets(buf, sizeof(buf), stdin)) break;
        if (buf[0] != 'y' && buf[0] != 'Y') break;
    }
}
