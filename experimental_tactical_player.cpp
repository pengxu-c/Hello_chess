// ============================================================================
// tactical_player.cpp —— Tactical++ 实现
//
// 与 Minimax++ 的三点本质差异：
//   1. 棋型识别用「定长窗口扫描」而非 scanLine 的连续段统计
//      → 跳三 X_XX、断口四 XX_XX、嵌空冲四 XX_XXX 全部可见
//   2. 用 VCF / VCT 递归追胜替代固定 2 步的硬编码规则
//      → 任意长度的冲四链、活三链都能算到
//   3. 评估函数基于窗口潜力，断口棋型自动获得正确权重
//
// 所有对 board 的修改都严格配对撤销，place() 返回时棋盘与进入时一致。
// ============================================================================

#include "experimental_tactical_player.h"

#include <algorithm>
#include <vector>

namespace {

constexpr int kDR[4] = {0, 1, 1, 1};
constexpr int kDC[4] = {1, 0, 1, -1};

inline ChessType oppOf(ChessType c) {
    return static_cast<ChessType>(-static_cast<int>(c));
}

// ---------------------------------------------------------------------------
// 定长窗口扫描
// ---------------------------------------------------------------------------
// 枚举棋盘上所有长度为 winLen 的连续窗口（横 / 竖 / 两条斜线）。
// 对每个窗口：
//   若无对方子 → 是己方潜力窗口，窗口内每个空位记录「己方子数」的最大值
//   若无己方子 → 同理记录对方潜力
//
// 为什么这能识别断口：窗口是「连续的 winLen 格」，与格子内部排布无关。
//   XX_XX  在 5 格窗口里 = 4 子 1 空 → 冲四，成五点就是中间那格
//   X_XX   在 5 格窗口里 = 3 子 2 空 → 三，有发展潜力
// 而 scanLine 是 while (at()==color)，遇空格立刻停，
//   XX_XX 会被看成两个彼此孤立的 2 连，彻底看不见威胁。
struct WindowScan {
    int n = 0;
    std::vector<int> bestMy;   // 每格：所在最优「无对方子」窗口中的己方子数
    std::vector<int> bestOpp;

    void scan(const Board& b, ChessType me, ChessType opp) {
        n = b.size();
        const int w = b.winLen();
        bestMy.assign(n * n, 0);
        bestOpp.assign(n * n, 0);
        for (int d = 0; d < 4; ++d) {
            const int dr = kDR[d], dc = kDC[d];
            for (int r = 0; r < n; ++r) {
                for (int c = 0; c < n; ++c) {
                    const int er = r + dr * (w - 1), ec = c + dc * (w - 1);
                    if (!b.inBounds(er, ec)) continue;
                    int my = 0, op = 0;
                    for (int k = 0; k < w; ++k) {
                        const ChessType t = b.at(r + dr * k, c + dc * k);
                        if (t == me) ++my;
                        else if (t == opp) ++op;
                    }
                    if (my > 0 && op > 0) continue;   // 混合窗口：已死
                    for (int k = 0; k < w; ++k) {
                        const int rr = r + dr * k, cc = c + dc * k;
                        if (b.at(rr, cc) != ChessType::None) continue;
                        const int idx = rr * n + cc;
                        if (op == 0 && my > bestMy[idx]) bestMy[idx] = my;
                        if (my == 0 && op > bestOpp[idx]) bestOpp[idx] = op;
                    }
                }
            }
        }
    }
};

// 成五点：落该子立即成五（bestMy == winLen-1 的空位）
inline void collectFivePoints(const Board& b, const WindowScan& ws,
                              std::vector<Pos>& out) {
    const int w = b.winLen(), n = ws.n;
    out.clear();
    for (int i = 0; i < n * n; ++i) {
        if (ws.bestMy[i] == w - 1) out.push_back({i / n, i % n});
    }
}

// 假设已在 (r,c) 落 me（调用前 board 上该点仍为空，函数内部临时落子后撤销），
// 计算落子后 me 的成五点数量。
// baseFive：落子前已存在的成五点（不含 (r,c)，通常由上一层传入）
inline int fiveCountAfter(Board& b, ChessType me, ChessType opp,
                          int r, int c, const std::vector<Pos>& baseFive) {
    const int w = b.winLen(), n = b.size();
    b.set(r, c, me);

    static std::vector<int> seen;
    seen.clear();
    for (const Pos& p : baseFive) {
        if (p.r == r && p.c == c) continue;
        seen.push_back(p.r * n + p.c);
    }

    for (int d = 0; d < 4; ++d) {
        const int dr = kDR[d], dc = kDC[d];
        for (int k = 0; k < w; ++k) {
            const int sr = r - dr * k, sc = c - dc * k;
            const int er = sr + dr * (w - 1), ec = sc + dc * (w - 1);
            if (!b.inBounds(sr, sc) || !b.inBounds(er, ec)) continue;
            int my = 0, op = 0, hole = -1;
            for (int t = 0; t < w; ++t) {
                const int rr = sr + dr * t, cc = sc + dc * t;
                const ChessType v = b.at(rr, cc);
                if (v == me) ++my;
                else if (v == opp) ++op;
                else hole = rr * n + cc;
            }
            // 落子后该窗口须恰好差一子成五，且无对方子 → 唯一空位即成五点
            if (op > 0 || my != w - 1 || hole < 0) continue;
            if (std::find(seen.begin(), seen.end(), hole) == seen.end())
                seen.push_back(hole);
        }
    }
    b.set(r, c, ChessType::None);
    return static_cast<int>(seen.size());
}

// 找出「落该点后能形成活四（成五点 >= 2）」的点。
// 用途：识别活三 —— 若某方能造活四的点有 2 个以上，说明它已是活三，
// 对手单点堵不完，必须立刻处理。（Minimax++ 的 critical 层干的就是这件事）
inline void collectLiveFourMakers(Board& b, ChessType me, ChessType opp,
                                  const WindowScan& ws, std::vector<Pos>& out) {
    const int w = b.winLen(), n = ws.n;
    std::vector<Pos> baseFive;
    collectFivePoints(b, ws, baseFive);
    out.clear();
    for (int i = 0; i < n * n; ++i) {
        if (ws.bestMy[i] != w - 2) continue;
        const int r = i / n, c = i % n;
        if (b.at(r, c) != ChessType::None) continue;
        if (fiveCountAfter(b, me, opp, r, c, baseFive) >= 2) out.push_back({r, c});
    }
}

// 某方「一步成五」的点（用于开局接管与防守封堵）
inline Pos findImmediateWin(Board& b, ChessType me, ChessType opp) {
    WindowScan ws;
    ws.scan(b, me, opp);
    std::vector<Pos> five;
    collectFivePoints(b, ws, five);
    return five.empty() ? Pos{-1, -1} : five.front();
}

// 生成候选：具备潜力（任一方潜力 >= 1）的空位，按己方潜力降序
inline std::vector<Pos> potentialMoves(const Board& b, const WindowScan& ws,
                                       int minLevel) {
    const int n = ws.n;
    std::vector<std::pair<int, int>> sc;
    for (int i = 0; i < n * n; ++i) {
        if (b.at(i / n, i % n) != ChessType::None) continue;
        const int v = std::max(ws.bestMy[i], ws.bestOpp[i]);
        if (v < minLevel) continue;
        sc.push_back({std::max(ws.bestMy[i] * 2, ws.bestOpp[i] * 2 - 1), i});
    }
    std::sort(sc.begin(), sc.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    std::vector<Pos> out;
    out.reserve(sc.size());
    for (const auto& s : sc) out.push_back({s.second / n, s.second % n});
    return out;
}

// ---------------------------------------------------------------------------
// VCF：连续冲四取胜（Victory of Continuous Four）
// ---------------------------------------------------------------------------
// 每一步都走「冲四 / 活四」这类对方必须应的先手，直到成五。
// 对手在每个冲四后只有唯一必应点，因此分支因子很低，可以搜得很深。
struct VcfCtx {
    int nodes = 0;
    int limit = 400000;
    bool over() { return ++nodes > limit; }
};

bool vcfSearch(Board& b, ChessType me, ChessType opp, int depth,
               VcfCtx& ctx, Pos& best) {
    if (depth <= 0 || ctx.over()) return false;

    WindowScan ws;
    ws.scan(b, me, opp);

    std::vector<Pos> baseFive;
    collectFivePoints(b, ws, baseFive);
    if (!baseFive.empty()) { best = baseFive.front(); return true; }

    const int w = b.winLen();
    // 候选：能造出「差一子成五」窗口的点（bestMy == w-2 → 冲四；== w-1 已在上一步处理）
    std::vector<std::pair<int, Pos>> cands;
    const int n = ws.n;
    for (int i = 0; i < n * n; ++i) {
        if (ws.bestMy[i] == w - 2) {
            const int v = ws.bestMy[i] * 2 + ws.bestOpp[i];
            cands.push_back({v, {i / n, i % n}});
        }
    }
    if (cands.empty()) return false;
    std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    for (const auto& cd : cands) {
        const Pos m = cd.second;
        // 试探：落 m 后己方成五点数量
        const int fcnt = fiveCountAfter(b, me, opp, m.r, m.c, baseFive);
        if (fcnt == 0) continue;              // 不是冲四，VCF 不取

        b.set(m.r, m.c, me);
        if (fcnt >= 2) {                      // 活四：无解，直接胜
            b.set(m.r, m.c, ChessType::None);
            best = m;
            return true;
        }
        // 冲四：对手只有唯一必应点
        std::vector<Pos> block(1);
        {
            WindowScan ws2;
            ws2.scan(b, me, opp);
            int idx = -1;
            for (int i = 0; i < n * n; ++i)
                if (ws2.bestMy[i] == w - 1) { idx = i; break; }
            if (idx < 0) { b.set(m.r, m.c, ChessType::None); continue; }
            block[0] = {idx / n, idx % n};
        }
        // 对手落子前先确认这一手没有让对手直接反杀
        b.set(block[0].r, block[0].c, opp);
        Pos sub;
        const bool win = vcfSearch(b, me, opp, depth - 1, ctx, sub);
        b.set(block[0].r, block[0].c, ChessType::None);
        b.set(m.r, m.c, ChessType::None);
        if (win) { best = m; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// VCT：冲四 + 活三追胜（Victory by Continuous Threat）
// ---------------------------------------------------------------------------
// 我方走先手（冲四或活三），对手的所有应手都必须被击败 —— 这是 AND-OR 搜索：
// 我方节点是 OR（有一个走法成立即可），对手节点是 AND（所有应法都要赢）。
struct VctCtx {
    int nodes = 0;
    int limit = 200000;
    bool over() { return ++nodes > limit; }
};

bool vctSearch(Board& b, ChessType me, ChessType opp, int depth,
               VctCtx& ctx, Pos& best);

// 计算对手在「我方刚落 m」之后必须应对的点集
inline std::vector<Pos> forcedReplies(Board& b, ChessType me, ChessType opp,
                                      const Pos& m) {
    const int w = b.winLen(), n = b.size();
    std::vector<Pos> out;
    std::vector<int> seen;

    // 1) 己方成五点 —— 对手必须堵
    {
        WindowScan ws;
        ws.scan(b, me, opp);
        for (int i = 0; i < n * n; ++i)
            if (ws.bestMy[i] == w - 1) out.push_back({i / n, i % n});
        if (!out.empty()) return out;   // 冲四/活四：唯一（或双）必应点
    }
    // 2) 己方活四点（能形成活四的点）—— 活三的防守点
    std::vector<Pos> empty;   // 当前无成五点
    for (int d = 0; d < 4; ++d) {
        const int dr = kDR[d], dc = kDC[d];
        for (int k = 0; k < w; ++k) {
            const int sr = m.r - dr * k, sc = m.c - dc * k;
            const int er = sr + dr * (w - 1), ec = sc + dc * (w - 1);
            if (!b.inBounds(sr, sc) || !b.inBounds(er, ec)) continue;
            int my = 0, op = 0;
            for (int t = 0; t < w; ++t) {
                const ChessType v = b.at(sr + dr * t, sc + dc * t);
                if (v == me) ++my; else if (v == opp) ++op;
            }
            if (op > 0 || my != w - 2) continue;
            for (int t = 0; t < w; ++t) {
                const int rr = sr + dr * t, cc = sc + dc * t;
                if (b.at(rr, cc) != ChessType::None) continue;
                const int idx = rr * n + cc;
                if (std::find(seen.begin(), seen.end(), idx) == seen.end()) {
                    seen.push_back(idx);
                    out.push_back({rr, cc});
                }
            }
        }
    }
    return out;
}

bool vctSearch(Board& b, ChessType me, ChessType opp, int depth,
               VctCtx& ctx, Pos& best) {
    if (depth <= 0 || ctx.over()) return false;

    // 先用 VCF 快检：能直接冲死就不必走活三
    // （深度压到 8：VCT 每个节点都会做一次快检，太深会拖垮性能）
    {
        VcfCtx vc;
        Pos vb;
        if (vcfSearch(b, me, opp, 8, vc, vb)) { best = vb; return true; }
    }

    WindowScan ws;
    ws.scan(b, me, opp);
    const int w = b.winLen(), n = ws.n;

    std::vector<Pos> baseFive;
    collectFivePoints(b, ws, baseFive);
    if (!baseFive.empty()) { best = baseFive.front(); return true; }

    // 先手候选：能造四（w-2）或造活四的点
    std::vector<std::pair<int, Pos>> cands;
    for (int i = 0; i < n * n; ++i) {
        if (ws.bestMy[i] < w - 2) continue;
        if (ws.bestMy[i] == w - 1) continue;   // 成五点已在上面处理
        cands.push_back({ws.bestMy[i] * 2 + ws.bestOpp[i], {i / n, i % n}});
    }
    // 再补活三点（bestMy == w-3）。这类点数量多、分支大，必须限量，
    // 否则 VCT 会指数爆炸 —— 这里只取潜力最高的 8 个。
    std::vector<std::pair<int, Pos>> threes;
    for (int i = 0; i < n * n; ++i) {
        if (ws.bestMy[i] != w - 3) continue;
        threes.push_back({ws.bestMy[i] * 2 + ws.bestOpp[i], {i / n, i % n}});
    }
    std::sort(threes.begin(), threes.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    if (static_cast<int>(threes.size()) > 8) threes.resize(8);
    cands.insert(cands.end(), threes.begin(), threes.end());

    std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    if (static_cast<int>(cands.size()) > 12) cands.resize(12);

    for (const auto& cd : cands) {
        const Pos m = cd.second;
        b.set(m.r, m.c, me);

        std::vector<Pos> replies = forcedReplies(b, me, opp, m);
        if (replies.empty() || replies.size() > 8) {   // 不是先手 / 分支太多，剪掉
            b.set(m.r, m.c, ChessType::None);
            continue;
        }
        // 对手的 AND 节点：所有应法都必须被击败
        // 注意：节点数超限而中断时不得判定为「必胜」，否则会走出假杀招
        bool allWin = true;
        bool aborted = false;
        Pos sub;
        for (const Pos& rp : replies) {
            b.set(rp.r, rp.c, opp);
            if (!vctSearch(b, me, opp, depth - 1, ctx, sub)) allWin = false;
            b.set(rp.r, rp.c, ChessType::None);
            if (!allWin) break;
            if (ctx.over()) { aborted = true; break; }
        }
        b.set(m.r, m.c, ChessType::None);
        if (allWin && !aborted) { best = m; return true; }
    }
    return false;
}

}  // namespace

// ===========================================================================
// TacticalPP
// ===========================================================================

TacticalPP::TacticalPP(Judge& judge) : judge_(judge) {}

bool TacticalPP::isHuman() const { return false; }
bool TacticalPP::needsDelay() const { return true; }
const char* TacticalPP::name() const { return "Tactical++"; }

// 窗口潜力评估：能正确处理断口棋型
int TacticalPP::evaluate(const Board& board, ChessType me) const {
    const ChessType opp = oppOf(me);
    WindowScan ws;
    ws.scan(board, me, opp);
    const int w = board.winLen(), n = ws.n;

    // 潜力越大价值增长越快；差一子成五（w-1）必须压倒一切
    // 表长取 15：bestMy/bestOpp 最大为 w-1，w 上限 15（configureRules），
    // 故下标恒 ≤14。原 32 长表令 i≥16 时 1<<(i*2) 超过 int 位宽，属未定义行为。
    static int tbl[15];
    static bool init = false;
    if (!init) {
        tbl[0] = 0;
        for (int i = 1; i < 15; ++i) tbl[i] = 1 << (i * 2);
        init = true;
    }

    int score = 0;
    for (int i = 0; i < n * n; ++i) {
        if (board.at(i / n, i % n) != ChessType::None) continue;
        const int a = ws.bestMy[i], d = ws.bestOpp[i];
        if (a >= w - 1)      score += 1000000;   // 自己有冲四/活四
        else                 score += tbl[a];    // a ≤ w-1 ≤ 14，恒在表内
        if (d >= w - 1)      score -= 1200000;   // 对手有冲四/活四，防守优先
        else                 score -= tbl[d] * 12 / 10;
    }
    return score;
}

int TacticalPP::minimax(Board& board, int depth, int alpha, int beta,
                        ChessType cur, ChessType me) {
    const ChessType opp = oppOf(me);
    // 终局检测：以刚落子方为准
    if (depth <= 0) return evaluate(board, me);

    WindowScan ws;
    ws.scan(board, me, opp);
    std::vector<Pos> moves = potentialMoves(board, ws, 1);
    if (moves.empty()) return evaluate(board, me);
    if (static_cast<int>(moves.size()) > kMaxCands) moves.resize(kMaxCands);

    // 先做一步胜负检测，避免地平线效应（Minimax++ 顶层正是漏了这一层）
    const int w = board.winLen();
    for (const Pos& m : moves) {
        const int idx = m.r * ws.n + m.c;
        if (cur == me && ws.bestMy[idx] == w - 1)
            return kInf - (kSearchDepth - depth);        // 己方直接成五
        if (cur != me && ws.bestOpp[idx] == w - 1)
            return -(kInf - (kSearchDepth - depth));     // 对方直接成五
    }

    const bool isMax = (cur == me);
    int best = isMax ? -kInf : kInf;
    for (const Pos& m : moves) {
        board.set(m.r, m.c, cur);
        int val;
        if (judge_.checkWin(board, m, cur)) {
            val = isMax ? (kInf - (kSearchDepth - depth))
                        : -(kInf - (kSearchDepth - depth));
        } else {
            val = minimax(board, depth - 1, alpha, beta, oppOf(cur), me);
        }
        board.set(m.r, m.c, ChessType::None);

        if (isMax) {
            if (val > best) best = val;
            if (best > alpha) alpha = best;
        } else {
            if (val < best) best = val;
            if (best < beta) beta = best;
        }
        if (beta <= alpha) break;
    }
    return best;
}

std::vector<Pos> TacticalPP::generateMoves(Board& board, ChessType me) {
    WindowScan ws;
    ws.scan(board, me, oppOf(me));
    std::vector<Pos> mv = potentialMoves(board, ws, 1);
    if (mv.empty()) {   // 空盘：走中心
        const int c = board.size() / 2;
        mv.push_back({c, c});
    }
    return mv;
}

Pos TacticalPP::place(Board& board, ChessType color) {
    const ChessType me = color, opp = oppOf(color);
    const int w = board.winLen();

    // 1) 己方一步成五 —— 立即取胜
    Pos p = findImmediateWin(board, me, opp);
    if (p.valid()) { markLastMove(p); return p; }

    // 2) 对方一步成五 —— 必须封堵
    p = findImmediateWin(board, opp, me);
    if (p.valid()) { markLastMove(p); return p; }

    // 3) 己方 VCF 连续冲四取胜（这是 Minimax++ 完全没有的能力）
    {
        VcfCtx ctx;
        Pos best;
        if (vcfSearch(board, me, opp, kVcfDepth, ctx, best)) {
            markLastMove(best);
            return best;
        }
    }

    // 4) 防守：成五点 → VCF → 活三，三级递进
    //    活三这一层最容易被漏掉：对方三连时还没有「成五点」，
    //    但已有 2 个以上能造活四的点，不处理就会在下一步被活四打死。
    std::vector<Pos> defense;   // 强制：只在其中选点

    {
        WindowScan wsOpp;       // 以对手为主视角扫描
        wsOpp.scan(board, opp, me);

        std::vector<Pos> oppFive;
        collectFivePoints(board, wsOpp, oppFive);
        if (!oppFive.empty()) {
            defense = oppFive;                       // 对方差一子成五：必堵
        } else {
            // 顺序很关键：必须先查「对方能否造活四」。
            // 只要存在一个点让对方落子即成活四，对方就已经是活三，必须立刻处理。
            // 若把 vcfSearch 放在前面，活三本身也会让 VCF 成立，
            // 会走进「抢占所有冲四起点」的宽泛分支，候选膨胀后必防点反而被挤掉。
            std::vector<Pos> lfm;
            collectLiveFourMakers(board, opp, me, wsOpp, lfm);
            if (!lfm.empty()) {
                // 活三 / 双威胁：候选就限定为这几个点，不再混入己方进攻点
                defense = lfm;
            } else {
                VcfCtx ctx;
                Pos t;
                if (vcfSearch(board, opp, me, kVcfDepth, ctx, t)) {
                    // 对方有连续冲四杀但尚未成活三：抢占其冲四起点
                    for (int i = 0; i < wsOpp.n * wsOpp.n; ++i)
                        if (wsOpp.bestMy[i] >= w - 2)
                            defense.push_back({i / wsOpp.n, i % wsOpp.n});
                }
            }
        }
    }

    // 5) 己方 VCT 追胜（无防守压力时）
    if (defense.empty()) {
        VctCtx ctx;
        Pos best;
        if (vctSearch(board, me, opp, kVctDepth, ctx, best)) {
            markLastMove(best);
            return best;
        }
    }

    // 6) 常规 alpha-beta 搜索
    std::vector<Pos> cands = defense.empty() ? generateMoves(board, me) : defense;

    if (cands.empty()) return {-1, -1};
    if (static_cast<int>(cands.size()) > kMaxCands) cands.resize(kMaxCands);

    // 用启发分排序提升剪枝效率
    {
        WindowScan ws;
        ws.scan(board, me, opp);
        std::vector<std::pair<int, Pos>> sc;
        for (const Pos& m : cands)
            sc.push_back({ws.bestMy[m.r * ws.n + m.c] * 2 +
                          ws.bestOpp[m.r * ws.n + m.c], m});
        std::sort(sc.begin(), sc.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        for (size_t i = 0; i < cands.size(); ++i) cands[i] = sc[i].second;
    }

    int bestVal = -kInf;
    Pos best = cands.front();
    for (const Pos& m : cands) {
        board.set(m.r, m.c, me);
        int val;
        if (judge_.checkWin(board, m, me)) {
            val = kInf;
        } else {
            val = minimax(board, kSearchDepth - 1, -kInf, kInf, opp, me);
        }
        board.set(m.r, m.c, ChessType::None);
        if (val > bestVal) { bestVal = val; best = m; }
    }
    markLastMove(best);
    return best;
}
