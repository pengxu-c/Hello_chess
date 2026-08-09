// ============================================================
// player.cpp - 棋手类实现
// HumanPlayer / EasyJudgeAI / PureGreed10 / PureGreed11 / MinimaxPP
// ============================================================
#include "player.h"
#include "ui.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ==================== 单一评分核 + 单点核 + 必胜类 + 禁用机制 + 选择器 ====================
//
// 评分系统设计目的：消除旧 crit1/crit2/crit3/crit4_mix 的重复计数与量纲混用，
// 保证主要威胁(一步必防位)分数严格高于次要威胁(活三发展位)，搜索主导 Minimax++。
//
// 单一评分核 segValue(count, openEnds)：几何级数梯度，对任意 WIN_LEN(3..N) 成立。
//   count=连续同色长度, openEnds=两端开放数(0/1/2)。返回该线段威胁度：
//     count >= WIN_LEN → 1000000 (已成连)
//     openEnds == 0    → 0       (死棋，两端封堵无威胁)
//     diff = WIN_LEN - count
//     活棋(openEnds==2): diff==1→100000, ==2→10000, ==3→1000, ==4→100, >=5→10
//     眠棋(openEnds==1)=对应活棋/10
//   梯度保证 活四(100000) ≫ 活三(10000) ≫ 活二(1000)，优先级严格单调。
//
// 单点核 pointScore(board,r,c,color,maxCap)：模拟在 (r,c) 落 color 子后，沿 4 方向
//   找最大连续+开放，取最大 segValue，并用 maxCap 截断。调用后恢复棋盘。
//   攻防同函数，color 传 me 或 opp 即可。取代旧 crit1/crit2/crit3 三个重叠函数。
//
// 各 AI 攻防权重（清晰单一）：
//   EasyJudge  = 防守
//   PG 1.0     = 防守
//   PG 1.1     = 防守 + 0.9*进攻
//   Minimax++  = 搜索主导 + 启发式(/1000)打破平局
//
// 必胜类：1步必胜(自己成连) + 2步必胜(下m后>=2个成连点)，所有AI优先调用。
// 对方一步成连检测 findOppOneStepWin：必胜类之后、禁用机制之前，优先堵对方一步成连位
//   （解决 must 内"成n连位"与"成n-1连位"被 maxCap 截断同分的问题）。
// 禁用机制：对方活(WIN_LEN-2)连及以上时禁随机，只在必防位置精确选。
// 随机机制：top3 + 极窄区间随机。
// --------------------------------------------------------------------

struct ScoredMove { int score, r, c; };

// 单一评分核：几何级数梯度，对任意 WIN_LEN(3..N) 成立，禁止硬编码 n=5
static int segValue(int count, int openEnds) {
    if (count >= WIN_LEN) return 1000000;   // 已成连，极大
    if (openEnds == 0)    return 0;         // 死棋，两端封堵无威胁
    int diff = WIN_LEN - count;             // 还差几连成胜，diff >= 1
    bool live = (openEnds == 2);
    int base;
    switch (diff) {
        case 1:  base = 100000; break;      // 活四/冲四
        case 2:  base =  10000; break;      // 活三/眠三
        case 3:  base =   1000; break;      // 活二/眠二
        case 4:  base =    100; break;      // 活一/眠一
        default: base =     10; break;      // diff >= 5
    }
    return live ? base : base / 10;         // 眠棋 = 活棋 / 10
}

// 单点核：模拟在 (r,c) 落 color 子后，沿 4 方向找最大连续+开放，取最大 segValue。
// maxCap 为上限截断（控制不同 AI 的量纲，如 150 或 1000000）。调用后恢复棋盘。
// 攻防同函数，color 传 me 或 opp 即可。
static int pointScore(Board& board, int r, int c, ChessType color, int maxCap) {
    board.set(r, c, color);
    int best = 0;
    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
        int count = 1;
        int nr = r + dr[d], nc = c + dc[d];
        while (board.inBounds(nr, nc) && board.at(nr, nc) == color) { count++; nr += dr[d]; nc += dc[d]; }
        bool openE = board.inBounds(nr, nc) && board.at(nr, nc) == ChessType::None;
        int pr = r - dr[d], pc = c - dc[d];
        while (board.inBounds(pr, pc) && board.at(pr, pc) == color) { count++; pr -= dr[d]; pc -= dc[d]; }
        bool openS = board.inBounds(pr, pc) && board.at(pr, pc) == ChessType::None;
        int s = segValue(count, (openS ? 1 : 0) + (openE ? 1 : 0));
        if (s > best) best = s;
    }
    board.set(r, c, ChessType::None);
    if (best > maxCap) best = maxCap;       // 上限截断
    return best;
}

// 独立连珠检查：在 (r,c) 落 color 子后是否形成 n 连（不依赖 Judge）
static bool inlineCheckN(const Board& board, int r, int c, ChessType color, int n) {
    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
        int count = 1;
        for (int s = 1; s < n; s++) { int nr = r + s*dr[d], nc = c + s*dc[d]; if (!board.inBounds(nr, nc) || board.at(nr, nc) != color) break; count++; }
        for (int s = 1; s < n; s++) { int nr = r - s*dr[d], nc = c - s*dc[d]; if (!board.inBounds(nr, nc) || board.at(nr, nc) != color) break; count++; }
        if (count >= n) return true;
    }
    return false;
}

// 对方一步成连检测：遍历空位模拟对方落子，若形成 WIN_LEN 连则返回该位（必须立即堵）。
// O(ROWS*COLS) 线段检查。对任意 WIN_LEN 成立。解决 must 内"成n连位"与"成n-1连位"被 maxCap 截断同分的问题。
static Pos findOppOneStepWin(Board& board, ChessType opp) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (board.at(r, c) != ChessType::None) continue;
            board.set(r, c, opp);
            bool win = inlineCheckN(board, r, c, opp, WIN_LEN);
            board.set(r, c, ChessType::None);
            if (win) return {r, c};
        }
    return {-1, -1};
}

// 必胜类：1步必胜(自己成连) + 2步必胜(下m后>=2个成连点，对方堵一个还有另一个)
// 所有 AI 在 place() 开头调用，检测到必胜直接返回，保证两步内获胜。
static Pos findWinMove(Board& board, ChessType color) {
    // 1步必胜
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (board.at(r, c) != ChessType::None) continue;
            board.set(r, c, color);
            bool win = inlineCheckN(board, r, c, color, WIN_LEN);
            board.set(r, c, ChessType::None);
            if (win) return {r, c};
        }
    // 2步必胜：下m后自己有>=2个成连点
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (board.at(r, c) != ChessType::None) continue;
            board.set(r, c, color);
            int winSpots = 0;
            for (int r2 = 0; r2 < ROWS && winSpots < 2; r2++)
                for (int c2 = 0; c2 < COLS && winSpots < 2; c2++) {
                    if (board.at(r2, c2) != ChessType::None) continue;
                    board.set(r2, c2, color);
                    if (inlineCheckN(board, r2, c2, color, WIN_LEN)) winSpots++;
                    board.set(r2, c2, ChessType::None);
                }
            board.set(r, c, ChessType::None);
            if (winSpots >= 2) return {r, c};
        }
    return {-1, -1};
}

// 禁用机制：模拟对方在每个空位落子，若形成 (WIN_LEN-1) 连则该位置必防。
// 与 EasyJudge 同理，能识别连续、跳棋等所有威胁模式。
static std::vector<Pos> findMustDefend(Board& board, ChessType opp) {
    std::vector<Pos> res;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (board.at(r, c) != ChessType::None) continue;
            board.set(r, c, opp);
            bool threat = inlineCheckN(board, r, c, opp, WIN_LEN - 1);
            board.set(r, c, ChessType::None);
            if (threat) res.push_back({r, c});
        }
    return res;
}

// 随机机制开关（默认关闭，关闭时 pickBestMove 退化为选最高分）
bool Player::randomEnabled = false;

// 不随机：选最高分
static Pos pickBestNoRandom(std::vector<ScoredMove>& scored) {
    if (scored.empty()) return {-1, -1};
    int bi = 0;
    for (int i = 1; i < (int)scored.size(); i++) if (scored[i].score > scored[bi].score) bi = i;
    return {scored[bi].r, scored[bi].c};
}

// top3 + 极窄区间随机：高分几乎不随机，低分少量随机（开关关闭时退化为选最高分）
static Pos pickBestMove(std::vector<ScoredMove>& scored) {
    if (!Player::randomEnabled) return pickBestNoRandom(scored);
    if (scored.empty()) return {-1, -1};
    std::sort(scored.begin(), scored.end(), [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
    int n = (int)scored.size(); if (n > 3) n = 3;   // top3
    int top1 = scored[0].score;
    int delta = (top1 >= 700) ? 1 : (top1 >= 500) ? 2 : (top1 >= 300) ? 3 : 5;
    int lower = top1 - delta;
    std::vector<ScoredMove> cand;
    for (int i = 0; i < n; i++) if (scored[i].score >= lower) cand.push_back(scored[i]);
    if (cand.empty()) cand.push_back(scored[0]);
    int idx = rand() % cand.size();
    return {cand[idx].r, cand[idx].c};
}
// ====================================================================

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


// EasyJudge: 必胜类 → 禁用机制 → 防守(0-150)
Pos EasyJudgeAI::place(Board& board, ChessType color) {
    ChessType opp = opponent(color);
    Pos win = findWinMove(board, color);
    if (win.r >= 0) return win;
    Pos oppWin = findOppOneStepWin(board, opp);
    if (oppWin.r >= 0) return oppWin;
    auto must = findMustDefend(board, opp);
    std::vector<ScoredMove> scored;
    if (!must.empty()) {
        for (auto& m : must) scored.push_back({pointScore(board, m.r, m.c, opp, 150), m.r, m.c});
        return pickBestNoRandom(scored);
    }
    for (int i = 0; i < ROWS; i++)
        for (int j = 0; j < COLS; j++) {
            if (board.at(i, j) != ChessType::None) continue;
            scored.push_back({pointScore(board, i, j, opp, 150), i, j});
        }
    return pickBestMove(scored);
}
bool EasyJudgeAI::isHuman() const { return false; }
const char* EasyJudgeAI::name() const { return "EasyJudge"; }

// ---------- PureGreed10：必胜类 → 禁用机制 → 防守(0-150) ----------
Pos PureGreed10::place(Board& board, ChessType color) {
    ChessType opp = opponent(color);
    Pos win = findWinMove(board, color);
    if (win.r >= 0) return win;
    Pos oppWin = findOppOneStepWin(board, opp);
    if (oppWin.r >= 0) return oppWin;
    auto must = findMustDefend(board, opp);
    std::vector<ScoredMove> scored;
    if (!must.empty()) {
        for (auto& m : must) scored.push_back({pointScore(board, m.r, m.c, opp, 150), m.r, m.c});
        return pickBestNoRandom(scored);
    }
    for (int i = 0; i < ROWS; i++)
        for (int j = 0; j < COLS; j++) {
            if (board.at(i, j) != ChessType::None) continue;
            scored.push_back({pointScore(board, i, j, opp, 150), i, j});
        }
    return pickBestMove(scored);
}
bool PureGreed10::isHuman() const { return false; }
const char* PureGreed10::name() const { return "PureGreed 1.0"; }

// ---------- PureGreed11：必胜类 → 禁用机制 → 防守(0-150) + 0.9*进攻(0-135) ----------
Pos PureGreed11::place(Board& board, ChessType color) {
    ChessType opp = opponent(color);
    Pos win = findWinMove(board, color);
    if (win.r >= 0) return win;
    Pos oppWin = findOppOneStepWin(board, opp);
    if (oppWin.r >= 0) return oppWin;
    auto must = findMustDefend(board, opp);
    std::vector<ScoredMove> scored;
    if (!must.empty()) {
        for (auto& m : must) {
            int s = pointScore(board, m.r, m.c, opp, 150) + (pointScore(board, m.r, m.c, color, 150) * 9 / 10);
            scored.push_back({s, m.r, m.c});
        }
        return pickBestNoRandom(scored);
    }
    for (int i = 0; i < ROWS; i++)
        for (int j = 0; j < COLS; j++) {
            if (board.at(i, j) != ChessType::None) continue;
            int s = pointScore(board, i, j, opp, 150) + (pointScore(board, i, j, color, 150) * 9 / 10);
            scored.push_back({s, i, j});
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

// 顶层决策：必胜类 → 对方一步成连 → 禁用机制 → 搜索主导 + 启发式打破平局
//   评分 = minimax_val(±kInf=±1e8) + (pointScore(me,1e6) + pointScore(opp,1e6)) / 1000
//   搜索主导：minimax_val(±1e8) 占绝对主导，启发式项(/1000，最大约1100)仅在搜索分不出高低时打破平局；
//   终局附近启发式占比<0.01%，非终局弱局面占比≤10-20%。
Pos MinimaxPP::place(Board& board, ChessType color) {
    ChessType opp = opponent(color);

    // 0) 必胜类：1步或2步必胜
    Pos win = findWinMove(board, color);
    if (win.r >= 0) return win;

    // 0.5) 对方一步成连：必须立即堵
    Pos oppWin = findOppOneStepWin(board, opp);
    if (oppWin.r >= 0) return oppWin;

    auto moves = generateMoves(board);
    if (moves.empty()) return { -1, -1 };

    // 1) 禁用机制：对方有活 (WIN_LEN-2) 连及以上 → 只在必防位置中精确选（不随机）
    auto must = findMustDefend(board, opp);
    const auto& cands = must.empty() ? moves : must;

    // 2) 搜索主导 + 启发式打破平局
    std::vector<ScoredMove> scored;
    for (const auto& m : cands) {
        board.set(m.r, m.c, color);
        int val = minimax(board, kDepth - 1, -kInf, kInf, opp, false, color);
        board.set(m.r, m.c, ChessType::None);
        int s = val + (pointScore(board, m.r, m.c, color, 1000000) + pointScore(board, m.r, m.c, opp, 1000000)) / 1000;
        scored.push_back({s, m.r, m.c});
    }
    if (!must.empty()) return pickBestNoRandom(scored);
    return pickBestMove(scored);
}

bool MinimaxPP::isHuman() const { return false; }
bool MinimaxPP::needsDelay() const { return false; }
const char* MinimaxPP::name() const { return "Minimax++"; }
