// ============================================================
// player.cpp - 棋手类实现
// HumanPlayer / EasyJudgeAI / PureGreed10 / PureGreed11
// ============================================================
#include "player.h"
#include "ui.h"
#include <cstdlib>

// ---------- HumanPlayer ----------
HumanPlayer::HumanPlayer(UI& ui) : ui_(ui) {}

// 人类落子：本帧有点击则返回点击位置，否则返回无效
Pos HumanPlayer::place(Board& board, ChessType color) {
    if (!ui_.hasClick()) return { -1, -1 };
    Pos p = ui_.clickPos();
    ui_.clearClick();
    return p;
}
bool HumanPlayer::isHuman() const { return true; }
const char* HumanPlayer::name() const { return "Human"; }

// ---------- EasyJudgeAI ----------
EasyJudgeAI::EasyJudgeAI(Judge& judge, Stats& stats) : judge_(judge), stats_(stats) {}

// 检测在 (r,c) 落对方子后是否形成五连（即此处需防守）
bool EasyJudgeAI::canBlockFive(Board& board, int r, int c, ChessType oppColor) {
    ChessType old = board.at(r, c);
    board.set(r, c, oppColor);
    bool win = judge_.checkFive(board, { r, c }, oppColor);
    board.set(r, c, old);
    return win;
}

// 超简单 AI：对方步数<30 时优先防输，否则随机落子
Pos EasyJudgeAI::place(Board& board, ChessType color) {
    ChessType oppColor = opponent(color);
    if (stats_.count(oppColor) < 60) {
        for (int i = 0; i < ROWS; i++) {
            for (int j = 0; j < COLS; j++) {
                if (board.at(i, j) == ChessType::None && canBlockFive(board, i, j, oppColor)) {
                    return { i, j };
                }
            }
        }
    }
    // 收集所有空位，随机选一个
    int (*empty)[2] = new int[ROWS * COLS][2];
    int count = 0;
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLS; j++) {
            if (board.at(i, j) == ChessType::None) {
                empty[count][0] = i;
                empty[count][1] = j;
                count++;
            }
        }
    }
    Pos result = { -1, -1 };
    if (count > 0) {
        int idx = rand() % count;
        result = { empty[idx][0], empty[idx][1] };
    }
    delete[] empty;
    return result;
}
bool EasyJudgeAI::isHuman() const { return false; }
const char* EasyJudgeAI::name() const { return "EasyJudge"; }

// ---------- PureGreed10：纯防守评分 ----------
// 仅评估对方威胁，选防守价值最高的位置
Pos PureGreed10::place(Board& board, ChessType color) {
    ChessType oppColor = opponent(color);
    int bestScore = -1;
    int bestR = -1, bestC = -1;
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLS; j++) {
            if (board.at(i, j) != ChessType::None) continue;
            int blockScore = 0;
            for (int d = 0; d < 4; d++) {
                int oppCount = 0;
                // 正方向扫描对方连续子数
                for (int step = 1; step <= 5; step++) {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else break;
                }
                // 反方向扫描
                for (int step = 1; step <= 5; step++) {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else break;
                }
                // 防守评分表
                if (oppCount >= 6) blockScore += 1000000;
                else if (oppCount >= 5) blockScore += 40000;
                else if (oppCount >= 4) blockScore += 20000;
                else if (oppCount >= 3) blockScore += 2000;
                else if (oppCount >= 2) blockScore += 200;
                else if (oppCount >= 1) blockScore += 1;
            }
            if (blockScore > bestScore) {
                bestScore = blockScore;
                bestR = i;
                bestC = j;
            }
        }
    }
    return { bestR, bestC };
}
bool PureGreed10::isHuman() const { return false; }
const char* PureGreed10::name() const { return "PureGreed 1.0"; }

// ---------- PureGreed11：攻防评分 ----------
// 同时评估自身进攻与对方威胁，选总分最高位置
Pos PureGreed11::place(Board& board, ChessType color) {
    ChessType oppColor = opponent(color);
    int bestScore = -1;
    int bestR = -1, bestC = -1;
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLS; j++) {
            int diew[4] = { 0 };  // 各方向己方被堵端数
            int dieb[4] = { 0 };  // 各方向对方被堵端数
            if (board.at(i, j) != ChessType::None) continue;
            int selfScore = 0, blockScore = 0;
            for (int d = 0; d < 4; d++) {
                // 己方连续子数（正反方向）
                int selfCount = 0;
                for (int step = 1; step <= 5; step++) {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == color) selfCount++;
                    else if (board.at(ni, nj) == oppColor) { diew[d]++; break; }
                    else break;
                }
                for (int step = 1; step <= 5; step++) {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == color) selfCount++;
                    else if (board.at(ni, nj) == oppColor) { diew[d]++; break; }
                    else break;
                }
                // 对方连续子数（正反方向）
                int oppCount = 0;
                for (int step = 1; step <= 5; step++) {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else if (board.at(ni, nj) == color) { dieb[d]++; break; }
                    else break;
                }
                for (int step = 1; step <= 5; step++) {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else if (board.at(ni, nj) == color) { dieb[d]++; break; }
                    else break;
                }
                // 进攻评分（区分活/死棋）
                if (selfCount >= 6) selfScore += 1000000;
                else if (selfCount == 5) { if (diew[d] >= 2) selfScore += 100; else selfScore += 40000; }
                else if (selfCount == 4) { if (diew[d] >= 2) selfScore += 30; else selfScore += 10000; }
                else if (selfCount == 3) { if (diew[d] >= 2) selfScore += 20; else selfScore += 1000; }
                else if (selfCount >= 2) { if (diew[d] >= 2) selfScore += 5; else selfScore += 100; }
                // 防守评分
                if (oppCount >= 6) blockScore += 50000;
                else if (oppCount >= 5) { if (dieb[d] >= 2) blockScore += 100; else blockScore += 30000; }
                else if (oppCount >= 4) { if (dieb[d] >= 2) blockScore += 80; else blockScore += 20000; }
                else if (oppCount >= 3) { if (dieb[d] >= 2) blockScore += 10; else blockScore += 2000; }
                else if (oppCount >= 2) { if (dieb[d] >= 2) blockScore += 1; else blockScore += 200; }
                else if (oppCount >= 1) blockScore += 1;
            }
            int total = selfScore + blockScore;
            if (total > bestScore) {
                bestScore = total;
                bestR = i;
                bestC = j;
            }
        }
    }
    // 兜底：理论上不会触发，保证棋盘满时仍可继续
    if (bestR == -1) {
        for (int i = 0; i < ROWS; i++)
            for (int j = 0; j < COLS; j++)
                if (board.at(i, j) == ChessType::None) return { i, j };
    }
    return { bestR, bestC };
}
bool PureGreed11::isHuman() const { return false; }
const char* PureGreed11::name() const { return "PureGreed 1.1"; }
