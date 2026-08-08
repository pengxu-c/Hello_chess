// ============================================================
// player.cpp - 棋手类实现
// HumanPlayer / EasyJudgeAI / PureGreed10 / PureGreed11 / MinimaxPP
// ============================================================
#include "player.h"
#include "ui.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ==================== 通用评分工具 ====================
// 三个 AI 的内部原始分各不相同，统一映射到 0-750 整数，
// 记录 top8，在 top1 下方合理区间内随机选择，增加对局多样性。
// 设计约束：必胜 → 750，此时区间仅含 750 的位置（随机选也必胜）。

// 评分位置结构（映射后的 0-750 分 + 坐标）
struct ScoredMove {
    int score;  // 0-750
    int r, c;
};

// 非负原始分 → 0-750（PureGreed10/11 用）。
// raw >= 1000000(必胜级) → 750；其余对数压缩，使分值分布均匀。
static int mapTo750(int raw) {
    if (raw >= 1000000) return 750;
    if (raw <= 0) return 0;
    return (int)(749.0 * std::log(1.0 + raw) / std::log(1.0 + 1000000.0));
}

// minimax 值(±kInf) → 0-750（MinimaxPP 用）。
// +kInf → 750(必胜)，-kInf → 0(必败)，0 → 375(中性)，正负两侧对数压缩。
static int mapMinimaxTo750(int val) {
    const int INF = 100000000;
    if (val >= INF - 100) return 750;
    if (val <= -INF + 100) return 0;
    if (val >= 0) {
        return 375 + (int)(375.0 * std::log(1.0 + val) / std::log(1.0 + 1000000.0));
    } else {
        return 375 - (int)(375.0 * std::log(1.0 - val) / std::log(1.0 + 1000000.0));
    }
}

// 在 scored 列表中：降序排序取 top8，在 top1 下方 delta 区间内随机选。
// 区间半径 delta：
//   top1 == 750(必胜) → delta=0，只选 750 的位置（多个则随机）；
//   否则 delta = max(3, (750-top1)/10)，高分窄区间、低分宽区间。
static Pos pickBestMove(std::vector<ScoredMove>& scored) {
    if (scored.empty()) return { -1, -1 };
    std::sort(scored.begin(), scored.end(),
              [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
    int n = (int)scored.size();
    if (n > 8) n = 8;                          // 只看 top 8
    int top1 = scored[0].score;
    int rawDelta = (750 - top1) / 10;
    int delta = (top1 >= 750) ? 0 : (rawDelta < 3 ? 3 : rawDelta);   // 必胜→0；高分窄、低分宽
    if(delta>=20)  delta=20;   //不能超过20了
    int lower = top1 - delta;
    // 在 top8 中收集落在 [lower, top1] 区间的候选
    std::vector<ScoredMove> cand;
    for (int i = 0; i < n; i++) {
        if (scored[i].score >= lower) cand.push_back(scored[i]);
    }
    if (cand.empty()) cand.push_back(scored[0]);   // 保险
    int idx = rand() % cand.size();
    return { cand[idx].r, cand[idx].c };
}
// ====================================================

// 防守威胁评分（按 WIN_LEN 自动分级，PureGreed10 用）。
// diff = WIN_LEN - count 越小威胁越大，分值越高。
static int threatScore(int count) {
    if (count >= WIN_LEN) return 1000000;
    if (count <= 0) return 0;
    int diff = WIN_LEN - count;
    switch (diff) {
        case 1: return 40000;   // 活四：对方下一步成五
        case 2: return 20000;   // 活三
        case 3: return 2000;    // 活二
        case 4: return 200;     // 活一
        default: return 1;
    }
}

// Minimax 窗口评分（按 WIN_LEN 自动分级）。
// 注：当前 MinimaxPP::evaluate 已改用连续线段评估，此函数保留供参考/扩展。
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
// 仅评估对方威胁，映射到 0-750，top8 区间随机选
Pos PureGreed10::place(Board& board, ChessType color) {
    ChessType oppColor = opponent(color);
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    std::vector<ScoredMove> scored;
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
            scored.push_back({ mapTo750(blockScore), i, j });
        }
    }
    return pickBestMove(scored);
}
bool PureGreed10::isHuman() const { return false; }
const char* PureGreed10::name() const { return "PureGreed 1.0"; }

// ---------- PureGreed11：攻防评分 ----------
// 同时评估自身进攻与对方威胁，映射到 0-750，top8 区间随机选
Pos PureGreed11::place(Board& board, ChessType color) {
    ChessType oppColor = opponent(color);
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    std::vector<ScoredMove> scored;
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
                // diew[d]>=2 表示该方向两端都被对方封堵(死棋)，分值大幅降低
                int selfDiff = WIN_LEN - selfCount;
                if (selfCount >= WIN_LEN) selfScore += 1000000;
                else if (selfDiff == 1) { if (diew[d] >= 2) selfScore += 100; else selfScore += 40000; }   // 活四/冲四
                else if (selfDiff == 2) { if (diew[d] >= 2) selfScore += 30; else selfScore += 10000; }    // 活三/眠三
                else if (selfDiff == 3) { if (diew[d] >= 2) selfScore += 20; else selfScore += 1000; }     // 活二/眠二
                else if (selfDiff >= 4) { if (diew[d] >= 2) selfScore += 5; else selfScore += 100; }       // 活一/眠一
                // 防守评分（对方威胁，dieb[d]>=2 表示两端被己方封堵）
                int oppDiff = WIN_LEN - oppCount;
                if (oppCount >= WIN_LEN) blockScore += 50000;
                else if (oppDiff == 1) { if (dieb[d] >= 2) blockScore += 100; else blockScore += 30000; }  // 必防
                else if (oppDiff == 2) { if (dieb[d] >= 2) blockScore += 80; else blockScore += 20000; }
                else if (oppDiff == 3) { if (dieb[d] >= 2) blockScore += 10; else blockScore += 2000; }
                else if (oppDiff == 4) { if (dieb[d] >= 2) blockScore += 1; else blockScore += 200; }
                else blockScore += 1;
            }
            int total = selfScore + blockScore;
            scored.push_back({ mapTo750(total), i, j });
        }
    }
    return pickBestMove(scored);
}
bool PureGreed11::isHuman() const { return false; }
const char* PureGreed11::name() const { return "PureGreed 1.1"; }

// ---------- Minimax++：alpha-beta 剪枝搜索 ----------
MinimaxPP::MinimaxPP(Judge& judge) : judge_(judge) {}

// 局面评估：沿 4 方向扫描所有"连续同色线段"，按线段长度与两端开放数评分。
// 相比旧版"窗口计数"，本版本能区分活棋(两端开放)/眠棋(一端开放)/死棋(两端封堵)，
// 避免把 "X.X.X" 与 "XXX.." 评成等价，从而正确反映连续威胁。
//   评分表(连续数 count, 开放端数 open):
//     count>=WIN_LEN          -> 1,000,000 (已成连)
//     open==0(死棋)           -> 0          (无威胁)
//     diff=WIN_LEN-count:
//       diff==1 (活四/冲四)   -> 活 200,000 / 眠 20,000
//       diff==2 (活三/眠三)   -> 活  20,000 / 眠  2,000
//       diff==3 (活二/眠二)   -> 活   2,000 / 眠    200
//       diff==4 (活一/眠一)   -> 活     200 / 眠     20
//       diff>=5               -> 活      20 / 眠      2
int MinimaxPP::evaluate(const Board& board, ChessType aiColor) const {
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    ChessType oppColor = opponent(aiColor);
    int score = 0;
    for (int d = 0; d < 4; d++) {
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                ChessType cur = board.at(r, c);
                if (cur == ChessType::None) continue;
                // 仅从"线段起点"开始计数：前一格不同色或越界，避免同一段被重复统计
                int pr = r - dr[d], pc = c - dc[d];
                if (board.inBounds(pr, pc) && board.at(pr, pc) == cur) continue;
                // 向前延伸统计连续同色长度
                int count = 0;
                int nr = r, nc = c;
                while (board.inBounds(nr, nc) && board.at(nr, nc) == cur) {
                    count++;
                    nr += dr[d];
                    nc += dc[d];
                }
                // 两端是否为空位(开放)；越界或被对方封堵视为不开放
                bool openStart = board.inBounds(pr, pc) && board.at(pr, pc) == ChessType::None;
                bool openEnd   = board.inBounds(nr, nc) && board.at(nr, nc) == ChessType::None;
                int openEnds = (openStart ? 1 : 0) + (openEnd ? 1 : 0);
                // 按连续长度与开放端数查表评分
                int segScore;
                if (count >= WIN_LEN) {
                    segScore = 1000000;
                } else if (openEnds == 0) {
                    segScore = 0;                       // 两端封堵，不可能成连，无威胁
                } else {
                    int diff = WIN_LEN - count;
                    bool live = (openEnds == 2);
                    switch (diff) {
                        case 1: segScore = live ? 200000 : 20000; break;
                        case 2: segScore = live ?  20000 :  2000; break;
                        case 3: segScore = live ?   2000 :   200; break;
                        case 4: segScore = live ?    200 :    20; break;
                        default: segScore = live ?     20 :     2; break;
                    }
                }
                // 己方线段加分，对方线段减分
                if (cur == aiColor) score += segScore;
                else                score -= segScore;
            }
        }
    }
    return score;
}

// 生成候选着法：棋盘空时返回中心；否则收集已有棋子周围 kRadius 内的空位。
// 用 nearby 二维布尔数组去重，避免同一空位被多个棋子的邻域重复加入。
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

// alpha-beta 递归搜索。
//   isMax=true  → AI 方回合(最大化)，落 curColor(=aiColor)，胜则返回 +kInf-(kDepth-depth)
//   isMax=false → 对方回合(最小化)，落 curColor(=opp)，  胜则返回 -kInf+(kDepth-depth)
// 胜负距离加权：越浅层获胜分越多，促使 AI 优先选择最快取胜/最迟告负的路径。
int MinimaxPP::minimax(Board& board, int depth, int alpha, int beta,
                       ChessType curColor, bool isMax, ChessType aiColor) {
    if (depth == 0) return evaluate(board, aiColor);          // 叶子节点：静态评估
    auto moves = generateMoves(board);
    if (moves.empty()) return evaluate(board, aiColor);       // 无候选着法：静态评估
    if (isMax) {
        int best = -kInf;
        for (const auto& m : moves) {
            board.set(m.r, m.c, curColor);
            if (judge_.checkWin(board, m, curColor, WIN_LEN)) {
                board.set(m.r, m.c, ChessType::None);
                return kInf - (kDepth - depth);   // AI 获胜，越浅赢越好
            }
            int val = minimax(board, depth - 1, alpha, beta, opponent(curColor), false, aiColor);
            board.set(m.r, m.c, ChessType::None);
            if (val > best) best = val;
            if (best > alpha) alpha = best;
            if (alpha >= beta) break;             // β 剪枝
        }
        return best;
    } else {
        int best = kInf;
        for (const auto& m : moves) {
            board.set(m.r, m.c, curColor);
            if (judge_.checkWin(board, m, curColor, WIN_LEN)) {
                board.set(m.r, m.c, ChessType::None);
                return -kInf + (kDepth - depth);  // 对方获胜，越浅输越糟
            }
            int val = minimax(board, depth - 1, alpha, beta, opponent(curColor), true, aiColor);
            board.set(m.r, m.c, ChessType::None);
            if (val < best) best = val;
            if (best < beta) beta = best;
            if (alpha >= beta) break;             // α 剪枝
        }
        return best;
    }
}

// 顶层决策：先抢自己成五，再防对方成五，否则 alpha-beta 搜索选最优
Pos MinimaxPP::place(Board& board, ChessType color) {
    auto moves = generateMoves(board);
    if (moves.empty()) return { -1, -1 };
    ChessType oppColor = opponent(color);

    // 1) 自己能直接成连 → 立即获胜
    for (const auto& m : moves) {
        board.set(m.r, m.c, color);
        if (judge_.checkWin(board, m, color, WIN_LEN)) {
            board.set(m.r, m.c, ChessType::None);
            return m;
        }
        board.set(m.r, m.c, ChessType::None);
    }
    // 2) 对方能直接成连 → 必须在该位置防守（否则下一步必输）
    for (const auto& m : moves) {
        board.set(m.r, m.c, oppColor);
        if (judge_.checkWin(board, m, oppColor, WIN_LEN)) {
            board.set(m.r, m.c, ChessType::None);
            return m;
        }
        board.set(m.r, m.c, ChessType::None);
    }

    // 3) alpha-beta 搜索每个候选，映射到 0-750，top8 区间随机选。
    //    为保证 top8 各 val 精确，此处不更新 alpha（完整搜索每个候选）。
    std::vector<ScoredMove> scored;
    for (const auto& m : moves) {
        board.set(m.r, m.c, color);
        int val = minimax(board, kDepth - 1, -kInf, kInf, oppColor, false, color);
        board.set(m.r, m.c, ChessType::None);
        scored.push_back({ mapMinimaxTo750(val), m.r, m.c });
    }
    return pickBestMove(scored);
}

bool MinimaxPP::isHuman() const { return false; }
bool MinimaxPP::needsDelay() const { return false; }
const char* MinimaxPP::name() const { return "Minimax++"; }
