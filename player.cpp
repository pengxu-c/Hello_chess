// ============================================================
// player.cpp - 棋手类实现
// HumanPlayer / EasyJudgeAI / PureGreed10 / PureGreed11 / MinimaxPP
// ============================================================
#include "player.h"
#include "ui.h"
#include <cstdlib>

// 防守威胁评分（按 WIN_LEN 自动分级，PureGreed10 用）
static int threatScore(int count) {
    if (count >= WIN_LEN) return 1000000;
    if (count <= 0) return 0;
    int diff = WIN_LEN - count;
    switch (diff) {
        case 1: return 40000;
        case 2: return 20000;
        case 3: return 2000;
        case 4: return 200;
        default: return 1;
    }
}

// Minimax 窗口评分（按 WIN_LEN 自动分级）
static int windowScore(int count) {
    if (count >= WIN_LEN) return 1000000;
    if (count <= 0) return 0;
    int diff = WIN_LEN - count;
    switch (diff) {
        case 1: return 50000;
        case 2: return 5000;
        case 3: return 500;
        case 4: return 50;
        default: return 1;
    }
}

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
bool HumanPlayer::needsDelay() const { return false; }
const char* HumanPlayer::name() const { return "Human"; }

// ---------- EasyJudgeAI ----------
EasyJudgeAI::EasyJudgeAI(Judge& judge, Stats& stats) : judge_(judge), stats_(stats) {}

// 检测在 (r,c) 落对方子后是否形成 (WIN_LEN-1) 连（即此处需防守）
bool EasyJudgeAI::canBlockFive(Board& board, int r, int c, ChessType oppColor) {
    ChessType old = board.at(r, c);
    board.set(r, c, oppColor);
    bool win = judge_.checkWin(board, { r, c }, oppColor, WIN_LEN - 1);
    board.set(r, c, old);
    return win;
}

// 超简单 AI：对方步数<60 时优先防输，否则随机落子
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
                for (int step = 1; step <= WIN_LEN - 1; step++) {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else break;
                }
                // 反方向扫描
                for (int step = 1; step <= WIN_LEN - 1; step++) {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else break;
                }
                blockScore += threatScore(oppCount);
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
                for (int step = 1; step <= WIN_LEN - 1; step++) {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == color) selfCount++;
                    else if (board.at(ni, nj) == oppColor) { diew[d]++; break; }
                    else break;
                }
                for (int step = 1; step <= WIN_LEN - 1; step++) {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == color) selfCount++;
                    else if (board.at(ni, nj) == oppColor) { diew[d]++; break; }
                    else break;
                }
                // 对方连续子数（正反方向）
                int oppCount = 0;
                for (int step = 1; step <= WIN_LEN - 1; step++) {
                    int ni = i + step * dr[d], nj = j + step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else if (board.at(ni, nj) == color) { dieb[d]++; break; }
                    else break;
                }
                for (int step = 1; step <= WIN_LEN - 1; step++) {
                    int ni = i - step * dr[d], nj = j - step * dc[d];
                    if (!board.inBounds(ni, nj)) break;
                    if (board.at(ni, nj) == oppColor) oppCount++;
                    else if (board.at(ni, nj) == color) { dieb[d]++; break; }
                    else break;
                }
                // 进攻评分（按 WIN_LEN 分级，区分活/死棋）
                int selfDiff = WIN_LEN - selfCount;
                if (selfCount >= WIN_LEN) selfScore += 1000000;
                else if (selfDiff == 1) { if (diew[d] >= 2) selfScore += 100; else selfScore += 40000; }
                else if (selfDiff == 2) { if (diew[d] >= 2) selfScore += 30; else selfScore += 10000; }
                else if (selfDiff == 3) { if (diew[d] >= 2) selfScore += 20; else selfScore += 1000; }
                else if (selfDiff >= 4) { if (diew[d] >= 2) selfScore += 5; else selfScore += 100; }
                // 防守评分
                int oppDiff = WIN_LEN - oppCount;
                if (oppCount >= WIN_LEN) blockScore += 50000;
                else if (oppDiff == 1) { if (dieb[d] >= 2) blockScore += 100; else blockScore += 30000; }
                else if (oppDiff == 2) { if (dieb[d] >= 2) blockScore += 80; else blockScore += 20000; }
                else if (oppDiff == 3) { if (dieb[d] >= 2) blockScore += 10; else blockScore += 2000; }
                else if (oppDiff == 4) { if (dieb[d] >= 2) blockScore += 1; else blockScore += 200; }
                else blockScore += 1;
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

// ---------- Minimax++：alpha-beta 剪枝搜索 ----------
MinimaxPP::MinimaxPP(Judge& judge) : judge_(judge) {}

// 局面评估：遍历所有长度 WIN_LEN 窗口，按己方/对方纯窗口子数评分
int MinimaxPP::evaluate(const Board& board, ChessType aiColor) const {
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    ChessType oppColor = opponent(aiColor);
    int score = 0;
    for (int d = 0; d < 4; d++) {
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                if (!board.inBounds(r + (WIN_LEN - 1) * dr[d], c + (WIN_LEN - 1) * dc[d])) continue;
                int self = 0, opp = 0;
                for (int i = 0; i < WIN_LEN; i++) {
                    ChessType t = board.at(r + i * dr[d], c + i * dc[d]);
                    if (t == aiColor) self++;
                    else if (t == oppColor) opp++;
                }
                if (self > 0 && opp == 0) score += windowScore(self);
                else if (opp > 0 && self == 0) score -= windowScore(opp);
            }
        }
    }
    return score;
}

// 生成候选着法：已有棋子周围 kRadius 内的空位
std::vector<Pos> MinimaxPP::generateMoves(const Board& board) const {
    std::vector<Pos> moves;
    bool hasAny = false;
    for (int r = 0; r < ROWS && !hasAny; r++)
        for (int c = 0; c < COLS; c++)
            if (board.at(r, c) != ChessType::None) { hasAny = true; break; }
    if (!hasAny) { moves.push_back({ ROWS / 2, COLS / 2 }); return moves; }
    std::vector<std::vector<bool>> nearby(ROWS, std::vector<bool>(COLS, false));
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (board.at(r, c) == ChessType::None) continue;
            for (int dr = -kRadius; dr <= kRadius; dr++) {
                for (int dc = -kRadius; dc <= kRadius; dc++) {
                    int nr = r + dr, nc = c + dc;
                    if (board.inBounds(nr, nc) && board.at(nr, nc) == ChessType::None)
                        nearby[nr][nc] = true;
                }
            }
        }
    }
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (nearby[r][c]) moves.push_back({ r, c });
    return moves;
}

// alpha-beta 递归搜索
int MinimaxPP::minimax(Board& board, int depth, int alpha, int beta,
                       ChessType curColor, bool isMax, ChessType aiColor) {
    if (depth == 0) return evaluate(board, aiColor);
    auto moves = generateMoves(board);
    if (moves.empty()) return evaluate(board, aiColor);
    if (isMax) {
        int best = -kInf;
        for (const auto& m : moves) {
            board.set(m.r, m.c, curColor);
            if (judge_.checkWin(board, m, curColor, WIN_LEN)) {
                board.set(m.r, m.c, ChessType::None);
                return kInf - (kDepth - depth);   // 越浅赢越好
            }
            int val = minimax(board, depth - 1, alpha, beta, opponent(curColor), false, aiColor);
            board.set(m.r, m.c, ChessType::None);
            if (val > best) best = val;
            if (best > alpha) alpha = best;
            if (alpha >= beta) break;
        }
        return best;
    } else {
        int best = kInf;
        for (const auto& m : moves) {
            board.set(m.r, m.c, curColor);
            if (judge_.checkWin(board, m, curColor, WIN_LEN)) {
                board.set(m.r, m.c, ChessType::None);
                return -kInf + (kDepth - depth);
            }
            int val = minimax(board, depth - 1, alpha, beta, opponent(curColor), true, aiColor);
            board.set(m.r, m.c, ChessType::None);
            if (val < best) best = val;
            if (best < beta) beta = best;
            if (alpha >= beta) break;
        }
        return best;
    }
}

// 顶层：枚举候选着法，选评估最高者
Pos MinimaxPP::place(Board& board, ChessType color) {
    auto moves = generateMoves(board);
    if (moves.empty()) return { -1, -1 };
    Pos bestMove = moves[0];
    int bestVal = -kInf;
    int alpha = -kInf, beta = kInf;
    for (const auto& m : moves) {
        board.set(m.r, m.c, color);
        if (judge_.checkWin(board, m, color, WIN_LEN)) {
            board.set(m.r, m.c, ChessType::None);
            return m;   // 直接获胜
        }
        int val = minimax(board, kDepth - 1, alpha, beta, opponent(color), false, color);
        board.set(m.r, m.c, ChessType::None);
        if (val > bestVal) { bestVal = val; bestMove = m; }
        if (bestVal > alpha) alpha = bestVal;
    }
    return bestMove;
}

bool MinimaxPP::isHuman() const { return false; }
bool MinimaxPP::needsDelay() const { return false; }
const char* MinimaxPP::name() const { return "Minimax++"; }
