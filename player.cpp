// ============================================================
// player.cpp - 棋手类实现
// HumanPlayer / GreedyScoringAI / MinimaxPP
// 必胜/必防威胁检测统一由 ThreatDetector（threat.h/.cpp）提供，
// 本文件仅保留：评分核(segValue/pointScore)、候选选择器、各 AI 决策流程。
// ============================================================
#include "player.h"
#include "threat.h"
#include "ui.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <random>

// ==================== 评分核 + 候选选择器 ====================
//
// 评分系统职责：把"某一个空位对双方的威胁/价值"量化为分数，供贪心 AI
// 与 Minimax++ 的启发式排序/打破平局使用。必胜/必防的判定不在评分层，
// 而由 ThreatDetector 精确给出（避免把"必防"埋进分数里导致量纲混乱）。
//
// 单一评分核 segValue(count, openEnds, winLen)：几何级数梯度，对任意 winLen(>=4) 成立。
//   count=连续同色长度, openEnds=两端开放数(0/1/2), winLen=连珠获胜数。返回该线段威胁度：
//     count >= winLen → 1000000 (已成连)
//     openEnds == 0    → 0       (死棋，两端封堵无威胁)
//     diff = winLen - count
//     活棋(openEnds==2): diff==1→100000, ==2→10000, ==3→1000, ==4→100, >=5→10
//     眠棋(openEnds==1)=对应活棋/10
//   梯度保证 活四(100000) ≫ 活三(10000) ≫ 活二(1000)，优先级严格单调。
//
// 单点核 pointScore(board,r,c,color,maxCap)：模拟在 (r,c) 落 color 子后，沿 4 方向
//   找最大连续+开放，取最大 segValue，并用 maxCap 截断。调用后恢复棋盘。
//   攻防同函数，color 传 me 或 opp 即可。
//
// 各 AI 攻防权重（清晰单一）：
//   EasyJudge  = 防守
//   PG 1.0     = 防守
//   PG 1.1     = 防守 + 0.9*进攻
//   Minimax++  = 搜索主导 + 启发式(/1000)打破平局
//
// 随机机制：top3 + 极窄区间随机（开关 Player::randomEnabled，默认关闭）。
// --------------------------------------------------------------------

struct ScoredMove { int score, r, c; };

// 单一评分核：几何级数梯度，对任意 winLen(>=4) 成立，禁止硬编码 n=5
// winLen 由调用方传入（取自 Board::winLen()），消除对全局的依赖
static int segValue(int count, int openEnds, int winLen) {
    if (count >= winLen) return 1000000;      // 已成连，极大
    if (openEnds == 0)    return 0;           // 死棋，两端封堵无威胁
    int diff = winLen - count;                // 还差几连成胜，diff >= 1
    bool live = (openEnds == 2);
    int base;
    switch (diff) {
        case 1:  base = 100000; break;        // 活四/冲四
        case 2:  base =  33000; break;        // 活三/眠三
        case 3:  base =   1000; break;        // 活二/眠二
        case 4:  base =    100; break;        // 活一/眠一
        default: base =     10; break;        // diff >= 5
    }
    return live ? base : base / 10;           // 眠棋 = 活棋 / 10
}

// 单点核：模拟在 (r,c) 落 color 子后，沿 4 方向找最大连续+开放，取最大 segValue。
// maxCap 为上限截断（控制不同 AI 的量纲，如 150 或 1000000）。调用后恢复棋盘。
// 攻防同函数，color 传 me 或 opp 即可。winLen 取自 board.winLen()。
// 沿 4 方向调 scanLine 统一统计连续同色+开放端，消除手写计数循环。
static int pointScore(Board& board, int r, int c, ChessType color, int maxCap) {
    int winLen = board.winLen();
    board.set(r, c, color);
    int best = 0;
    int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
        LineInfo li = scanLine(board, r, c, dr[d], dc[d], color);
        int s = segValue(li.count, (li.openStart ? 1 : 0) + (li.openEnd ? 1 : 0), winLen);
        if (s > best) best = s;
    }
    board.set(r, c, ChessType::None);
    if (best > maxCap) best = maxCap;       // 上限截断
    return best;
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

// ---- 去重：删除候选集中重复的位置（随机选前消除重复，保持均匀权重）----
static void dedupPos(std::vector<Pos>& v) {
    std::vector<Pos> out;
    for (const auto& p : v) {
        bool seen = false;
        for (const auto& q : out) if (q.r == p.r && q.c == p.c) { seen = true; break; }
        if (!seen) out.push_back(p);
    }
    v = out;
}

// ---- 在 color 棋子周围 radius 内随机选空位；无空位则退化为全盘随机空位 ----
static Pos randomNear(const Board& board, ChessType color, int radius = 2) {
    std::vector<Pos> empties;
    int n = board.size();
    std::vector<bool> seen(static_cast<size_t>(n) * n, false);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++) {
            if (board.at(r, c) != color) continue;
            for (int dr = -radius; dr <= radius; dr++)
                for (int dc = -radius; dc <= radius; dc++) {
                    int nr = r + dr, nc = c + dc;
                    if (board.inBounds(nr, nc) && board.at(nr, nc) == ChessType::None) {
                        size_t idx = static_cast<size_t>(nr) * n + nc;
                        if (!seen[idx]) { seen[idx] = true; empties.push_back({ nr, nc }); }
                    }
                }
        }
    if (!empties.empty()) return empties[rand() % empties.size()];
    // 退化：对方棋子附近无空位（或对方尚未落子），全盘空位随机
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (board.at(r, c) == ChessType::None) empties.push_back({ r, c });
    if (empties.empty()) return { -1, -1 };
    return empties[rand() % empties.size()];
}

// ---------- EasyJudgeAI：随机 + 堵（最简 AI）----------
// 唯一决策依据是 ThreatDetector 的必胜/必防候选，不评分、不全盘搜索：
//   1) 己方 1 步 / 2 步必胜 → 直接下（能赢就赢，不随机）
//   2) 必防候选（对方 1 步成连 + 对方 2 步必胜第一步位 + 双活三创建位）去重后随机选一个
//   3) 无威胁 → 对方棋子附近随机落子
Pos EasyJudgeAI::place(Board& board, ChessType color) {
    ChessType opp = opponent(color);
    ThreatDetector td(board);

    // 1) 己方必胜：直接下
    Pos win1 = td.oneStepWin(color);
    if (win1.valid()) return win1;
    Pos win2 = td.twoStepWin(color, opp);
    if (win2.valid()) return win2;

    // 2) 必防候选：对方 1 步成连 + 对方强制胜第一步位（mustDefend），随机选一个
    std::vector<Pos> cands;
    Pos b1 = td.oneStepWin(opp);
    if (b1.valid()) cands.push_back(b1);
    for (const auto& p : td.mustDefend(opp)) cands.push_back(p);
    dedupPos(cands);
    if (!cands.empty()) return cands[rand() % cands.size()];

    // 3) 无威胁：对方棋子附近随机
    return randomNear(board, opp);
}
bool EasyJudgeAI::isHuman() const { return false; }
bool EasyJudgeAI::needsDelay() const { return true; }
const char* EasyJudgeAI::name() const { return "EasyJudge"; }

// ---------- GreedyScoringAI：统一攻防评分 AI ----------
// 一个实现覆盖任意难度档：attackWeight=0 纯防守（EasyJudge/PG1.0），>0 加入进攻（PG1.1）
GreedyScoringAI::GreedyScoringAI(double attackWeight, const char* displayName)
    : attackWeight_(attackWeight), name_(displayName) {}

// 决策流程（威胁层级由高到低，均复用 ThreatDetector）：
//   己方1步必胜 → 对方1步成连 → 己方2步必胜 → 对方强制胜第一步位 → 常规评分
Pos GreedyScoringAI::place(Board& board, ChessType color) {
    ChessType opp = opponent(color);
    ThreatDetector td(board);

    Pos win1 = td.oneStepWin(color);
    if (win1.valid()) return win1;
    Pos blk1 = td.oneStepWin(opp);
    if (blk1.valid()) return blk1;
    Pos win2 = td.twoStepWin(color, opp);
    if (win2.valid()) return win2;

    // 统一单点火评估：防守(对方价值) + 进攻权重*己方价值，maxCap 控制量纲
    auto scoreAt = [&](int r, int c, int maxCap) {
        int defense = pointScore(board, r, c, opp, maxCap);
        int offense = (attackWeight_ > 0)
                      ? (int)(pointScore(board, r, c, color, maxCap) * attackWeight_)
                      : 0;
        return defense + offense;
    };

    // 对方强制胜第一步位（活四/双冲四/四三/双活三）：高优先级必防，大 maxCap 区分威胁等级
    auto defend = td.mustDefend(opp);
    if (!defend.empty()) {
        std::vector<ScoredMove> cs;
        for (auto& m : defend) cs.push_back({scoreAt(m.r, m.c, 1000000), m.r, m.c});
        return pickBestNoRandom(cs);
    }

    // 常规全盘扫描：防守 + 进攻权重评分
    int n = board.size();
    std::vector<ScoredMove> scored;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            if (board.at(i, j) != ChessType::None) continue;
            scored.push_back({scoreAt(i, j, 150), i, j});
        }
    return pickBestMove(scored);
}
bool GreedyScoringAI::isHuman() const { return false; }
bool GreedyScoringAI::needsDelay() const { return true; }
const char* GreedyScoringAI::name() const { return name_.c_str(); }

// ---------- Minimax++：alpha-beta 剪枝搜索 ----------
MinimaxPP::MinimaxPP(Judge& judge) : judge_(judge) {}

// 局面评估：沿 4 方向扫描所有"连续同色线段"，按线段长度与两端开放数评分。
// 统一调用 segValue 评分核，与 pointScore 使用同一张评分表，消除量纲不一致。
//   评分表见 segValue：count>=winLen→1,000,000；open==0→0；活棋=base，眠棋=base/10
//   己方线段加分，对方线段减分。winLen/尺寸取自 board。
int MinimaxPP::evaluate(const Board& board, ChessType aiColor) const {
    int dr[] = { 0, 1, 1, 1 };
    int dc[] = { 1, 0, 1, -1 };
    int n = board.size();
    int winLen = board.winLen();
    int score = 0;
    for (int d = 0; d < 4; d++) {
        for (int r = 0; r < n; r++) {
            for (int c = 0; c < n; c++) {
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
                // 统一评分核（与 pointScore 同表）
                int segScore = segValue(count, openEnds, winLen);
                // 己方线段加分，对方线段减分
                if (cur == aiColor) score += segScore;
                else                score -= segScore;
            }
        }
    }
    return score;
}

// 生成候选着法：棋盘空时返回中心；否则收集已有棋子周围 kRadius 内的空位。
// 用静态布尔数组去重，避免递归中堆分配（性能关键）。
// static 缓冲安全：generateMoves 不递归，返回 vector 后缓冲可被内层重用。
std::vector<Pos> MinimaxPP::generateMoves(const Board& board) const {
    std::vector<Pos> moves;
    int n = board.size();
    bool hasAny = false;
    for (int r = 0; r < n && !hasAny; r++)
        for (int c = 0; c < n; c++)
            if (board.at(r, c) != ChessType::None) { hasAny = true; break; }
    if (!hasAny) { moves.push_back({ n / 2, n / 2 }); return moves; }
    static bool nearby[kMaxBoard][kMaxBoard];          // 静态缓冲，避免每次递归堆分配
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            nearby[r][c] = false;
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
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
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (nearby[r][c]) moves.push_back({ r, c });
    return moves;
}

// 初始化 Zobrist 随机数表（固定种子保证可复现）
void MinimaxPP::initZobrist() {
    std::mt19937_64 rng(0x123456789ABCDEFULL);
    for (int r = 0; r < kMaxBoard; r++)
        for (int c = 0; c < kMaxBoard; c++)
            for (int k = 0; k < 2; k++)
                zobrist_[r][c][k] = rng();
}

// 计算当前棋盘的 Zobrist 哈希
uint64_t MinimaxPP::boardHash(const Board& board) const {
    uint64_t h = 0;
    int n = board.size();
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++) {
            ChessType t = board.at(r, c);
            if (t == ChessType::Black)      h ^= zobrist_[r][c][0];
            else if (t == ChessType::White) h ^= zobrist_[r][c][1];
        }
    return h;
}

// alpha-beta 递归搜索 + 启发式排序 + Zobrist 置换表。
//   isMax=true  → AI 方回合(最大化)，落 curColor(=aiColor)，胜则返回 +kInf-(kDepth-depth)
//   isMax=false → 对方回合(最小化)，落 curColor(=opp)，  胜则返回 -kInf+(kDepth-depth)
// 胜负距离加权：越浅层获胜分越多，促使 AI 优先选择最快取胜/最迟告负的路径。
// hash 为当前局面 Zobrist 哈希，落子时异或更新，O(1) 维护。
int MinimaxPP::minimax(Board& board, int depth, int alpha, int beta,
                       ChessType curColor, bool isMax, ChessType aiColor, uint64_t hash) {
    // 置换表查询
    auto it = transTable_.find(hash);
    if (it != transTable_.end() && it->second.depth >= depth) {
        int v = it->second.value, f = it->second.flag;
        if (f == 0) return v;                       // exact
        if (f == 1 && v > alpha) alpha = v;         // lower bound
        if (f == 2 && v < beta)  beta  = v;         // upper bound
        if (alpha >= beta) return v;
    }

    if (depth == 0) return evaluate(board, aiColor);          // 叶子节点：静态评估
    auto moves = generateMoves(board);
    if (moves.empty()) return evaluate(board, aiColor);       // 无候选着法：静态评估

    // 启发式排序：按 pointScore 降序，优先搜索高分节点，大幅提升剪枝效率
    ChessType oppColor = opponent(curColor);
    std::vector<ScoredMove> sortedMoves;
    sortedMoves.reserve(moves.size());
    for (const auto& m : moves) {
        int s = pointScore(board, m.r, m.c, curColor, 1000000) + pointScore(board, m.r, m.c, oppColor, 1000000);
        sortedMoves.push_back({s, m.r, m.c});
    }
    std::sort(sortedMoves.begin(), sortedMoves.end(),
              [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });

    int origAlpha = alpha, origBeta = beta;
    // sign=+1 时本轮为 AI(最大化) 回合，sign=-1 为对方(最小化)回合
    int sign = isMax ? 1 : -1;
    int best = isMax ? -kInf : kInf;
    bool terminal = false;
    for (const auto& m : sortedMoves) {
        int colorIdx = (curColor == ChessType::Black) ? 0 : 1;
        uint64_t newHash = hash ^ zobrist_[m.r][m.c][colorIdx];
        board.set(m.r, m.c, curColor);
        // checkWin 不再传 length 参数，Judge 内部用 board.winLen()
        if (judge_.checkWin(board, {m.r, m.c}, curColor)) {
            board.set(m.r, m.c, ChessType::None);
            // 越浅层获胜/告负越极端：AI 优取胜越快分越高，对方优取胜越快分越低
            best = sign * (kInf - (kDepth - depth));
            terminal = true;
            break;
        }
        int val = minimax(board, depth - 1, alpha, beta, oppColor, !isMax, aiColor, newHash);
        board.set(m.r, m.c, ChessType::None);
        // 统一极大极小：sign=+1 取 max 并提升 alpha；sign=-1 取 min 并压低 beta
        if (sign * val > sign * best) best = val;
        int& bound = isMax ? alpha : beta;
        if (sign * best > sign * bound) bound = best;
        if (alpha >= beta) break;                 // α-β 剪枝
    }
    int flag = terminal ? 0 : (best <= origAlpha ? 2 : (best >= origBeta ? 1 : 0));
    transTable_[hash] = {depth, best, flag};
    return best;
}

// 顶层决策：必胜/必防层级（ThreatDetector）→ 合并防守候选 → 搜索主导 + 启发式打破平局
//   评分 = minimax_val(±kInf=±1e8) + (pointScore(me,1e6) + pointScore(opp,1e6)) / 1000
//   搜索主导：minimax_val(±1e8) 占绝对主导，启发式项(/1000)仅在搜索分不出高低时打破平局。
// 防守候选合并：对方强制胜第一步位（mustDefend，覆盖活四/双冲四/四三/双活三），
//   修复旧版"互斥选择"导致同时存在多威胁时只防其一、被另一个连杀的问题。
Pos MinimaxPP::place(Board& board, ChessType color) {
    ChessType opp = opponent(color);

    if (!zobristInited_) { initZobrist(); zobristInited_ = true; }
    transTable_.clear();                                    // 每次顶层决策重置置换表

    ThreatDetector td(board);

    // 1) 己方 1 步必胜  2) 对方 1 步成连（必须立即堵）  3) 己方 2 步必胜
    Pos win1 = td.oneStepWin(color);
    if (win1.valid()) return win1;
    Pos blk1 = td.oneStepWin(opp);
    if (blk1.valid()) return blk1;
    Pos win2 = td.twoStepWin(color, opp);
    if (win2.valid()) return win2;

    auto moves = generateMoves(board);
    if (moves.empty()) return { -1, -1 };

    // 合并防守候选：对方强制胜第一步位（活四/双冲四/四三/双活三），mustDefend 天然去重
    auto defenseMoves = td.mustDefend(opp);
    const auto& cands = !defenseMoves.empty() ? defenseMoves : moves;

    // 搜索主导 + 启发式打破平局
    uint64_t baseHash = boardHash(board);
    int colorIdxMe = (color == ChessType::Black) ? 0 : 1;
    std::vector<ScoredMove> scored;
    scored.reserve(cands.size());
    for (const auto& m : cands) {
        uint64_t newHash = baseHash ^ zobrist_[m.r][m.c][colorIdxMe];
        board.set(m.r, m.c, color);
        int val = minimax(board, kDepth - 1, -kInf, kInf, opp, false, color, newHash);
        board.set(m.r, m.c, ChessType::None);
        int s = val + (pointScore(board, m.r, m.c, color, 1000000) + pointScore(board, m.r, m.c, opp, 1000000)) / 1000;
        scored.push_back({s, m.r, m.c});
    }
    if (!defenseMoves.empty()) return pickBestNoRandom(scored);
    return pickBestMove(scored);
}

bool MinimaxPP::isHuman() const { return false; }
bool MinimaxPP::needsDelay() const { return false; }
const char* MinimaxPP::name() const { return "Minimax++"; }