// ============================================================================
// tactical_max.cpp - TacticalMax 引擎实现（Phase 1-4）
//
// 一句话架构：把"评估"从全盘扫描的函数，改造成增量维护的状态。
//
//   L0 增量窗口状态
//      预计算所有长度 w 的连续窗口（横/竖/两条斜线），建立「格子 → 所属窗口」索引。
//      make/unmake 只影响 ≤ 4w 个窗口，每个窗口的贡献是 O(1)，故单节点
//      评估从 O(n²·w)≈4500 次访问降到 O(4w)≈20 次。这是深度上得去的根因修复。
//
//   L1 棋型评估
//      每个窗口用「己方掩码 + 对手掩码 + 两端外侧格是否为空」判定棋型等级，
//      成五/活四/冲四/活三/眠三/活二 分档给分，活三与眠三相差 10 倍
//      （修复原实现两者同分的梯度断裂）。掩码 → 分值做懒缓存避免重复计算。
//
//   L2 搜索框架
//      迭代加深 + 硬性时间预算（deadline）+ Zobrist 置换表（深度优先替换）
//      + PVS + 杀手/历史启发 + 深度相关候选拓宽。
//
//   L3 追胜融合
//      VCF（连续冲四）与主搜索共享同一时间预算与置换表，叶子节点做 VCF 静态延伸，
//      消除"深度 4 看不到第 5 步连冲四"的地平线效应。
//
// 修复的 bug.txt 真 Bug：
//   Bug1「先截断后排序」—— 本实现所有候选统一在排序完成后才截断（见 genCands/decide）。
//   Bug2「VCF/VCT 预算各自独立」—— 本实现所有搜索共用 Impl::deadline 与 stopped 标志，
//        且节点计数超时检查统一走 timeUp()。
// ============================================================================

#include "tactical_max.h"
#include "threat.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr int kDR[4] = { 0, 1, 1, 1 };
constexpr int kDC[4] = { 1, 0, 1, -1 };

inline ChessType oppOf(ChessType c) {
    return static_cast<ChessType>(-static_cast<int>(c));
}

// ---- 分值常量：整数便于严格比对；关键比值 活四/冲四=10、活三/眠三=10、活四/活三=20 ----
enum : int {
    SC_FIVE        = 10000000,
    SC_LIVE_FOUR   = 1000000,
    SC_RUSH_FOUR   = 100000,
    SC_LIVE_THREE  = 50000,
    SC_SLEEP_THREE = 5000,
    SC_LIVE_TWO    = 1000,
    SC_SLEEP_TWO   = 100,
    SC_LIVE_ONE    = 10,
};

constexpr int kWinScore = 100000000;   // 胜分上限，评估被 clamp 在 ±(kWinScore/2) 之内
constexpr int kInf      = 1000000000;

inline int popcount32(uint32_t x) {
    int c = 0;
    while (x) { x &= x - 1; ++c; }
    return c;
}

// 掩码中唯一的 0 位下标（要求 popcount(mask) == w-1）；返回 -1 表示不满足
inline int loneEmptyBit(uint32_t mask, int w) {
    for (int k = 0; k < w; ++k) if (!(mask & (1u << k))) return k;
    return -1;
}

// ---------------------------------------------------------------------------
// 窗口棋型评估：纯函数，(己方掩码, 两端外侧是否为空) → 分值。
// 只依赖窗口自身结构，是 L1 的全部智能所在，便于单元测试与后续替换。
// ---------------------------------------------------------------------------
int windowValue(uint32_t selfMask, bool outAEmpty, bool outBEmpty, int w) {
    const int pc = popcount32(selfMask);
    if (pc == 0) return 0;
    if (pc == w) return SC_FIVE;

    if (pc == w - 1) {
        // 四：空位在端部且该端外侧为空 → 落外侧也能成五，是活四；否则冲四。
        const int h = loneEmptyBit(selfMask, w);
        if (h == 0)     return outAEmpty ? SC_LIVE_FOUR : SC_RUSH_FOUR;
        if (h == w - 1) return outBEmpty ? SC_LIVE_FOUR : SC_RUSH_FOUR;
        return SC_RUSH_FOUR;                       // 断口四 XX_XX / XX_XXX
    }

    // 三（2 个空位）与二（3 个空位）：用"落一子后是否升级"递归判定，
    // 保证跳三 X_XX、嵌空三 X_XX 等断口形态被正确识别。
    if (pc == w - 2) {
        const uint32_t empties = (~selfMask) & ((1u << w) - 1);
        for (int k = 0; k < w; ++k) {
            if (!(empties & (1u << k))) continue;
            const uint32_t m2 = selfMask | (1u << k);
            const int h = loneEmptyBit(m2, w);
            if (h == 0 && outAEmpty)     return SC_LIVE_THREE;
            if (h == w - 1 && outBEmpty) return SC_LIVE_THREE;
        }
        return SC_SLEEP_THREE;
    }
    if (pc == w - 3) {
        const uint32_t empties = (~selfMask) & ((1u << w) - 1);
        for (int k = 0; k < w; ++k) {
            if (!(empties & (1u << k))) continue;
            const uint32_t m2 = selfMask | (1u << k);
            const uint32_t e2 = (~m2) & ((1u << w) - 1);
            for (int j = 0; j < w; ++j) {
                if (!(e2 & (1u << j))) continue;
                const uint32_t m3 = m2 | (1u << j);
                const int h = loneEmptyBit(m3, w);
                if (h == 0 && outAEmpty)     return SC_LIVE_TWO;
                if (h == w - 1 && outBEmpty) return SC_LIVE_TWO;
            }
        }
        return SC_SLEEP_TWO;
    }
    return SC_LIVE_ONE * pc;                        // 更少子：极小噪声值
}

// ---- 预计算窗口 ----
struct WinRef { int wid; int k; };                  // 格子所属窗口 + 在该窗口内的位下标

struct WindowInfo {
    int cells[15] = { 0 };     // 窗口内格子索引（winLen ≤ 15）
    int outA = -1, outB = -1;  // 沿方向的外侧延伸位（-1 表示越界）
    uint32_t blackMask = 0;    // 运行时：黑子占据掩码
    uint32_t whiteMask = 0;    // 运行时：白子占据掩码
};

struct TTEntry {
    uint64_t key = 0;
    int depth = -1;
    int value = 0;
    int flag = 0;              // 0=exact, 1=lower, 2=upper
    int move = -1;             // r*n+c
};

}  // namespace

// ===========================================================================
// 引擎实现
// ===========================================================================
struct TacticalMax::Impl {
    // ---- 可调参数 ----
    int budgetMs   = 1500;      // 单步时间预算（硬上限）
    int maxDepth   = 12;        // 迭代加深上限
    int vcfDepth   = 16;        // 顶层 VCF 最大连续冲四层数
    int leafVcf    = 6;         // 叶子 VCF 静态延伸层数
    int vctDepth   = 6;         // 顶层 VCT 最大层数
    static constexpr int kTTBits = 18;
    static constexpr int kRadius = 2;   // 邻域活跃半径

    explicit Impl(Judge& j) : judge(j), tt(1u << kTTBits), rng(seedNow()) {}

    Judge& judge;

    // ---- 局面缓存（按 n / w 重建） ----
    int n = 0, w = 0;
    std::vector<WindowInfo> wins;
    std::vector<std::vector<WinRef>> refs;   // 格子 → 所属窗口列表
    std::vector<int> valCache;               // (mask<<2|outA<<1|outB) → 分值，-1 未算
    std::vector<uint64_t> zob;               // n*n*2
    std::vector<TTEntry> tt;
    std::vector<uint8_t> active;             // 是否邻近已有棋子（候选生成用）

    // ---- 增量状态 ----
    long long scoreB = 0, scoreW = 0;        // 窗口分累加（黑/白视角）
    std::vector<int> fiveB, fiveW;           // 成五点计数
    std::vector<int> fourB, fourW;           // 能形成四的点计数
    uint64_t hash_ = 0;

    // ---- 搜索瞬时状态 ----
    std::chrono::steady_clock::time_point startTp, deadline_;
    bool stopped = false;
    long long nodes = 0;
    int rootMove = -1;
    std::vector<std::array<int, 2>> killer;
    std::vector<int> history;

    std::mt19937 rng;

    static uint64_t seedNow() {
        return static_cast<uint64_t>(
                   std::chrono::steady_clock::now().time_since_epoch().count())
               ^ (static_cast<uint64_t>(std::random_device{}()) << 32)
               ^ static_cast<uint64_t>(std::random_device{}());
    }

    // ---------------- 基础工具 ----------------
    inline uint32_t fullMask() const { return (1u << w) - 1u; }
    inline int idxOf(int r, int c) const { return r * n + c; }
    inline bool isStone(const Board& b, int idx) const {
        return b.at(idx / n, idx % n) != ChessType::None;
    }
    inline bool outEmpty(const Board& b, int idx) const {
        return idx >= 0 && b.at(idx / n, idx % n) == ChessType::None;
    }

    // 掩码 → 分值（懒缓存，避免同一形态反复计算）
    int cachedValue(uint32_t mask, bool oa, bool ob) {
        const size_t key = (static_cast<size_t>(mask) << 2)
                         | (oa ? 2u : 0u) | (ob ? 1u : 0u);
        int v = valCache[key];
        if (v < 0) { v = windowValue(mask, oa, ob, w); valCache[key] = v; }
        return v;
    }

    // 时间/节点预算：每 1024 节点检查一次，避免频繁读时钟
    bool timeUp() {
        if (stopped) return true;
        if ((nodes & 1023) == 0) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - startTp).count();
            if (el >= budgetMs) stopped = true;
        }
        return stopped;
    }

    // ---------------- 索引构建 ----------------
    void buildIndex(const Board& b) {
        n = b.size();
        w = b.winLen();
        wins.clear();
        refs.assign(static_cast<size_t>(n) * n, {});
        valCache.assign(static_cast<size_t>(1) << (w + 2), -1);

        for (int d = 0; d < 4; ++d) {
            const int dr = kDR[d], dc = kDC[d];
            for (int r = 0; r < n; ++r) {
                for (int c = 0; c < n; ++c) {
                    const int er = r + dr * (w - 1), ec = c + dc * (w - 1);
                    if (!b.inBounds(er, ec)) continue;
                    const int wid = static_cast<int>(wins.size());
                    WindowInfo wi;
                    for (int k = 0; k < w; ++k) {
                        const int rr = r + dr * k, cc = c + dc * k;
                        wi.cells[k] = rr * n + cc;
                        refs[rr * n + cc].push_back({ wid, k });
                    }
                    const int ar = r - dr, ac = c - dc;
                    const int br = er + dr, bc = ec + dc;
                    wi.outA = b.inBounds(ar, ac) ? ar * n + ac : -1;
                    wi.outB = b.inBounds(br, bc) ? br * n + bc : -1;
                    wins.push_back(wi);
                }
            }
        }

        // Zobrist 随机数表
        zob.resize(static_cast<size_t>(n) * n * 2);
        for (auto& z : zob) z = (static_cast<uint64_t>(rng()) << 32) ^ rng();

        tt.assign(1u << kTTBits, TTEntry{});
        active.assign(static_cast<size_t>(n) * n, 0);
        fiveB.assign(static_cast<size_t>(n) * n, 0);
        fiveW.assign(static_cast<size_t>(n) * n, 0);
        fourB.assign(static_cast<size_t>(n) * n, 0);
        fourW.assign(static_cast<size_t>(n) * n, 0);
        history.assign(static_cast<size_t>(n) * n, 0);
    }

    // ---------------- 增量贡献维护 ----------------
    // sign = -1 移除旧贡献 / +1 添加新贡献；只读当前 mask，不修改
    void contribute(const Board& b, int wid, int sign) {
        WindowInfo& wi = wins[wid];
        const bool oa = outEmpty(b, wi.outA);
        const bool ob = outEmpty(b, wi.outB);
        for (int t = 0; t < 2; ++t) {
            const bool black = (t == 0);
            const uint32_t sm = black ? wi.blackMask : wi.whiteMask;
            const uint32_t om = black ? wi.whiteMask : wi.blackMask;
            long long& sc = black ? scoreB : scoreW;
            std::vector<int>& five = black ? fiveB : fiveW;
            std::vector<int>& four = black ? fourB : fourW;

            if (om != 0) continue;                       // 对手已污染，价值恒 0
            const int pc = popcount32(sm);
            sc += static_cast<long long>(sign) * cachedValue(sm, oa, ob);
            if (pc == w - 1) {
                const int h = loneEmptyBit(sm, w);
                if (h >= 0) five[wi.cells[h]] += sign;
            } else if (pc == w - 2) {
                const uint32_t e = (~sm) & fullMask();
                for (int k = 0; k < w; ++k)
                    if (e & (1u << k)) four[wi.cells[k]] += sign;
            }
        }
    }

    void markActive(int idx) {
        const int r = idx / n, c = idx % n;
        for (int dr = -kRadius; dr <= kRadius; ++dr)
            for (int dc = -kRadius; dc <= kRadius; ++dc) {
                const int nr = r + dr, nc = c + dc;
                if (inBounds(nr, nc)) active[nr * n + nc] = 1;
            }
    }
    bool inBounds(int r, int c) const { return r >= 0 && c >= 0 && r < n && c < n; }

    // 从当前棋盘整体重建增量状态（每步决策开始时调用一次）
    void resetState(const Board& b) {
        for (auto& wi : wins) { wi.blackMask = 0; wi.whiteMask = 0; }
        scoreB = scoreW = 0;
        std::fill(fiveB.begin(), fiveB.end(), 0);
        std::fill(fiveW.begin(), fiveW.end(), 0);
        std::fill(fourB.begin(), fourB.end(), 0);
        std::fill(fourW.begin(), fourW.end(), 0);
        std::fill(active.begin(), active.end(), 0);
        hash_ = 0;

        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c) {
                const ChessType t = b.at(r, c);
                if (t == ChessType::None) continue;
                const int idx = r * n + c;
                const int ci = (t == ChessType::Black) ? 0 : 1;
                hash_ ^= zob[static_cast<size_t>(idx) * 2 + ci];
                for (const auto& ref : refs[idx]) {
                    if (t == ChessType::Black) wins[ref.wid].blackMask |= (1u << ref.k);
                    else                       wins[ref.wid].whiteMask |= (1u << ref.k);
                }
                markActive(idx);
            }
        for (int wid = 0; wid < static_cast<int>(wins.size()); ++wid)
            contribute(b, wid, +1);
    }

    void makeMove(Board& b, int idx, ChessType color) {
        for (const auto& ref : refs[idx]) contribute(b, ref.wid, -1);
        b.set(idx / n, idx % n, color);
        hash_ ^= zob[static_cast<size_t>(idx) * 2 + (color == ChessType::Black ? 0 : 1)];
        for (const auto& ref : refs[idx]) {
            if (color == ChessType::Black) wins[ref.wid].blackMask |= (1u << ref.k);
            else                           wins[ref.wid].whiteMask |= (1u << ref.k);
        }
        for (const auto& ref : refs[idx]) contribute(b, ref.wid, +1);
        markActive(idx);
    }

    void unmakeMove(Board& b, int idx) {
        const ChessType color = b.at(idx / n, idx % n);
        for (const auto& ref : refs[idx]) contribute(b, ref.wid, -1);
        b.set(idx / n, idx % n, ChessType::None);
        hash_ ^= zob[static_cast<size_t>(idx) * 2 + (color == ChessType::Black ? 0 : 1)];
        for (const auto& ref : refs[idx]) {
            if (color == ChessType::Black) wins[ref.wid].blackMask &= ~(1u << ref.k);
            else                           wins[ref.wid].whiteMask &= ~(1u << ref.k);
        }
        for (const auto& ref : refs[idx]) contribute(b, ref.wid, +1);
    }

    // ---------------- 评估 ----------------
    int evaluate(ChessType me) const {
        const long long sm = (me == ChessType::Black) ? scoreB : scoreW;
        const long long so = (me == ChessType::Black) ? scoreW : scoreB;
        long long v = sm - so * 11 / 10;             // 对手威胁略加权，偏防守
        if (v >  kWinScore / 2) v =  kWinScore / 2;
        if (v < -kWinScore / 2) v = -kWinScore / 2;
        return static_cast<int>(v);
    }

    const std::vector<int>& fiveOf(ChessType c) const {
        return (c == ChessType::Black) ? fiveB : fiveW;
    }
    const std::vector<int>& fourOf(ChessType c) const {
        return (c == ChessType::Black) ? fourB : fourW;
    }
    int countFive(ChessType c) const {
        const auto& v = fiveOf(c);
        int cnt = 0;
        for (int x : v) if (x > 0) ++cnt;
        return cnt;
    }
    int firstFive(ChessType c) const {
        const auto& v = fiveOf(c);
        for (int i = 0; i < static_cast<int>(v.size()); ++i) if (v[i] > 0) return i;
        return -1;
    }

    // ---------------- 候选生成 ----------------
    // 单点启发：己方增益 + 阻断对手增益（含完整 make/unmake，保证掩码与棋盘一致）
    int pointScore(Board& b, int idx, ChessType me) {
        const ChessType opp = oppOf(me);
        const long long beforeMe = (me == ChessType::Black) ? scoreB : scoreW;
        const long long beforeOp = (opp == ChessType::Black) ? scoreB : scoreW;

        makeMove(b, idx, me);
        const long long afterMe = (me == ChessType::Black) ? scoreB : scoreW;
        const long long afterOp = (opp == ChessType::Black) ? scoreB : scoreW;
        unmakeMove(b, idx);

        const long long gain = afterMe - beforeMe;
        const long long block = beforeOp - afterOp;
        long long s = gain + block;
        if (s > 1000000000LL) s = 1000000000LL;
        return static_cast<int>(s);
    }

    struct Cand { int idx; int score; };

    // 候选生成：只扫"邻近已有棋子"的空位；排序后再截断（修复 bug.txt Bug1）
    std::vector<Cand> genCands(Board& b, ChessType me, int depth, int ply) {
        std::vector<Cand> out;
        for (int idx = 0; idx < n * n; ++idx) {
            if (!active[idx]) continue;
            if (isStone(b, idx)) continue;
            out.push_back({ idx, pointScore(b, idx, me) });
        }
        if (out.empty()) {                               // 极端：无活跃点 → 中心附近兜底
            const int mid = (n / 2) * n + n / 2;
            if (b.at(mid / n, mid % n) == ChessType::None) out.push_back({ mid, 0 });
        }

        // TT 主变、杀手着法额外加权（在排序前，保证不被截断丢掉）
        for (auto& c : out) {
            if (c.idx == rootMove) c.score += 400000000;
            if (ply < static_cast<int>(killer.size())) {
                if (c.idx == killer[ply][0]) c.score += 200000000;
                else if (c.idx == killer[ply][1]) c.score += 100000000;
            }
            c.score += std::min(history[c.idx], 1000000) * 8;
        }
        std::sort(out.begin(), out.end(), [](const Cand& a, const Cand& b2) {
            return a.score > b2.score;
        });

        // 深度相关候选拓宽；成五/冲四制造点强制保留（截断在排序之后，修复 bug.txt Bug1）
        int cap = (depth >= 7) ? 24 : (depth >= 4) ? 16 : 12;
        if (static_cast<int>(out.size()) > cap) {
            const auto& four = fourOf(me);
            const auto& five = fiveOf(me);
            std::vector<Cand> forced;
            for (int i = cap; i < static_cast<int>(out.size()); ++i) {
                const int idx = out[i].idx;
                if (five[idx] > 0 || four[idx] > 0) forced.push_back(out[i]);
            }
            out.resize(cap);
            for (auto& c : forced) out.push_back(c);     // 必走点不参与截断
        }
        return out;
    }

    // ---------------- 置换表 ----------------
    void ttStore(uint64_t key, int depth, int value, int flag, int move) {
        TTEntry& e = tt[key & (tt.size() - 1)];
        // 深度优先替换：同 key 直接覆盖；不同 key 时保留更深条目
        if (e.key == key || e.depth <= depth) e = { key, depth, value, flag, move };
    }
    bool ttProbe(uint64_t key, int depth, int alpha, int beta, int& value, int& move) {
        const TTEntry& e = tt[key & (tt.size() - 1)];
        if (e.key != key || e.depth < depth) return false;
        move = e.move;
        if (e.flag == 0) { value = e.value; return true; }
        if (e.flag == 1 && e.value >= beta)  { value = e.value; return true; }
        if (e.flag == 2 && e.value <= alpha) { value = e.value; return true; }
        return false;
    }

    // ---------------- VCF：连续冲四取胜（增量，共享预算） ----------------
    bool vcf(Board& b, ChessType me, int depth, Pos& best) {
        if (depth <= 0 || timeUp()) return false;
        ++nodes;
        const ChessType opp = oppOf(me);

        const int f = firstFive(me);
        if (f >= 0) { best = { f / n, f % n }; return true; }

        std::vector<Cand> cands;
        const auto& four = fourOf(me);
        for (int idx = 0; idx < n * n; ++idx) {
            if (four[idx] <= 0 || isStone(b, idx)) continue;
            cands.push_back({ idx, pointScore(b, idx, me) });
        }
        if (cands.empty()) return false;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b2) { return a.score > b2.score; });

        for (const Cand& c : cands) {
            if (timeUp()) return false;
            makeMove(b, c.idx, me);
            const int cnt = countFive(me);
            if (cnt >= 2) {                              // 活四：对手无法双堵
                unmakeMove(b, c.idx);
                best = { c.idx / n, c.idx % n };
                return true;
            }
            if (cnt == 1) {
                const int blk = firstFive(me);           // 冲四：唯一必应点
                if (blk >= 0 && !isStone(b, blk)) {
                    makeMove(b, blk, opp);
                    Pos sub;
                    const bool win = vcf(b, me, depth - 1, sub);
                    unmakeMove(b, blk);
                    unmakeMove(b, c.idx);
                    if (win) { best = { c.idx / n, c.idx % n }; return true; }
                    continue;
                }
            }
            unmakeMove(b, c.idx);
        }
        return false;
    }

    // 对手在我方落 m 后的必应点（用于 VCT 的 AND 节点）
    std::vector<int> forcedReplies(Board& b, ChessType me) {
        std::vector<int> out;
        const int f = firstFive(me);
        if (f >= 0) { out.push_back(f); return out; }    // 成五逼应（唯一/双点）
        const auto& four = fourOf(me);
        for (int idx = 0; idx < n * n; ++idx) {
            if (four[idx] <= 0 || isStone(b, idx)) continue;
            makeMove(b, idx, me);
            const int cnt = countFive(me);
            unmakeMove(b, idx);
            if (cnt >= 2) out.push_back(idx);            // 活四制造点：对方必防
        }
        return out;
    }

    // ---------------- VCT：冲四 + 活三追胜（AND-OR，共享预算） ----------------
    bool vct(Board& b, ChessType me, int depth, Pos& best) {
        if (depth <= 0 || timeUp()) return false;
        ++nodes;
        const ChessType opp = oppOf(me);

        { Pos v; if (vcf(b, me, leafVcf, v)) { best = v; return true; } }

        std::vector<Cand> cands;
        const auto& four = fourOf(me);
        for (int idx = 0; idx < n * n; ++idx) {
            if (four[idx] <= 0 || isStone(b, idx)) continue;
            cands.push_back({ idx, pointScore(b, idx, me) });
        }
        if (cands.empty()) return false;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b2) { return a.score > b2.score; });
        if (cands.size() > 12) cands.resize(12);

        for (const Cand& c : cands) {
            if (timeUp()) return false;
            makeMove(b, c.idx, me);
            std::vector<int> replies = forcedReplies(b, me);
            if (replies.empty() || replies.size() > 8) { unmakeMove(b, c.idx); continue; }
            bool allWin = true;
            for (int rp : replies) {
                if (isStone(b, rp)) continue;
                makeMove(b, rp, opp);
                Pos sub;
                if (!vct(b, me, depth - 1, sub)) allWin = false;
                unmakeMove(b, rp);
                if (!allWin || timeUp()) break;
            }
            unmakeMove(b, c.idx);
            if (allWin) { best = { c.idx / n, c.idx % n }; return true; }
        }
        return false;
    }

    // ---------------- 静态搜索（叶子 VCF 延伸） ----------------
    int quiesce(Board& b, ChessType me, int alpha, int beta, int ply) {
        ++nodes;
        const int stand = evaluate(me);
        if (stand >= beta) return stand;
        if (stand > alpha) alpha = stand;
        if (timeUp()) return alpha;

        // 叶子静态延伸：己方存在冲四制造点则尝试浅 VCF，消除地平线效应
        const auto& four = fourOf(me);
        bool hasFour = false;
        for (int idx = 0; idx < n * n; ++idx)
            if (four[idx] > 0 && !isStone(b, idx)) { hasFour = true; break; }
        if (hasFour) {
            Pos v;
            if (vcf(b, me, leafVcf, v)) return kWinScore - ply;
        }
        return alpha;
    }

    // ---------------- 主搜索：PVS + α-β ----------------
    int search(Board& b, int depth, int alpha, int beta, ChessType me, int ply) {
        ++nodes;
        if (timeUp()) return evaluate(me);
        if (depth <= 0) return quiesce(b, me, alpha, beta, ply);

        int ttMove = -1, ttVal = 0;
        if (ttProbe(hash_, depth, alpha, beta, ttVal, ttMove)) return ttVal;

        std::vector<Cand> cands = genCands(b, me, depth, ply);
        if (cands.empty()) return evaluate(me);

        // 一步成五直接返回（消除地平线）
        for (const Cand& c : cands) {
            if (fiveOf(me)[c.idx] > 0) {
                ttStore(hash_, depth, kWinScore - ply, 0, c.idx);
                return kWinScore - ply;
            }
        }

        const int origAlpha = alpha, origBeta = beta;
        const ChessType opp = oppOf(me);
        int best = -kInf, bestMove = cands.front().idx;
        bool first = true;

        for (const Cand& c : cands) {
            if (timeUp()) break;
            makeMove(b, c.idx, me);

            int val;
            if (judge.checkWin(b, { c.idx / n, c.idx % n }, me)) {
                val = kWinScore - ply;
            } else if (first) {
                val = -search(b, depth - 1, -beta, -alpha, opp, ply + 1);
            } else {
                val = -search(b, depth - 1, -alpha - 1, -alpha, opp, ply + 1);  // 零窗口探测
                if (val > alpha && val < beta)
                    val = -search(b, depth - 1, -beta, -alpha, opp, ply + 1);    // 失败重搜
            }
            unmakeMove(b, c.idx);

            if (timeUp()) break;
            if (val > best) { best = val; bestMove = c.idx; }
            if (val > alpha) alpha = val;
            if (alpha >= beta) {
                // 杀手着法
                if (ply < static_cast<int>(killer.size())) {
                    if (killer[ply][0] != c.idx) { killer[ply][1] = killer[ply][0]; killer[ply][0] = c.idx; }
                }
                history[c.idx] = std::min(history[c.idx] + depth * depth, 1000000);
                break;
            }
            first = false;
        }

        const int flag = (best <= origAlpha) ? 2 : (best >= origBeta ? 1 : 0);
        ttStore(hash_, depth, best, flag, bestMove);
        return best;
    }

    // ---------------- 迭代加深 ----------------
    Pos searchBest(Board& b, ChessType me) {
        std::vector<Cand> roots = genCands(b, me, 99, 0);   // 根层用最宽候选
        if (roots.empty()) return { -1, -1 };
        Pos bestPos = { roots.front().idx / n, roots.front().idx % n };
        int bestVal = -kInf;

        for (int depth = 2; depth <= maxDepth; ++depth) {
            int alpha = -kInf, beta = kInf;
            int localBest = -kInf;
            Pos localPos = bestPos;
            bool complete = true;

            for (const Cand& c : roots) {
                if (timeUp()) { complete = false; break; }
                makeMove(b, c.idx, me);
                int val;
                if (judge.checkWin(b, { c.idx / n, c.idx % n }, me)) {
                    val = kWinScore;
                } else {
                    val = -search(b, depth - 1, -beta, -alpha, oppOf(me), 1);
                }
                unmakeMove(b, c.idx);
                if (val > localBest) { localBest = val; localPos = { c.idx / n, c.idx % n }; }
                if (val > alpha) alpha = val;
            }

            if (complete) {                                  // 只采信完整搜完的层
                bestVal = localBest;
                bestPos = localPos;
                rootMove = localPos.r * n + localPos.c;
            }
            if (!complete) break;                            // 预算用尽，保留上一层结果
            if (bestVal > kWinScore / 2) break;              // 已见胜势，提前收敛
        }
        return bestPos;
    }

    // ---------------- 决策入口 ----------------
    Pos decide(Board& b, ChessType color) {
        // 尺寸 / 连珠数变化则重建索引（新局、换规则）
        if (b.size() != n || b.winLen() != w) buildIndex(b);
        resetState(b);

        nodes = 0;
        stopped = false;
        startTp = std::chrono::steady_clock::now();
        rootMove = -1;
        killer.assign(64, { -1, -1 });
        // history 半衰，保留跨步经验但避免长期饱和
        for (int& h : history) h /= 2;

        const ChessType me = color, opp = oppOf(color);

        // 1) 己方一步成五
        {
            const int f = firstFive(me);
            if (f >= 0) return { f / n, f % n };
        }
        // 2) 对方一步成五 → 必堵
        {
            const int f = firstFive(opp);
            if (f >= 0) return { f / n, f % n };
        }
        // 3) 己方 VCF 连续冲四取胜
        {
            Pos v;
            if (vcf(b, me, vcfDepth, v)) return v;
        }
        // 4) 对方威胁防守：活四/双冲四/四三/双活三 用现成 ThreatDetector 精确列点
        {
            ThreatDetector td(b);
            std::vector<int> def;
            for (const Pos& p : td.mustDefend(opp)) def.push_back(p.r * n + p.c);
            if (def.empty()) {
                Pos v;
                if (vcf(b, opp, leafVcf, v)) {               // 对方有 VCF 杀 → 抢占其冲四起点
                    const auto& four = fourOf(opp);
                    for (int idx = 0; idx < n * n; ++idx)
                        if (four[idx] > 0 && !isStone(b, idx)) def.push_back(idx);
                }
            }
            if (!def.empty()) {
                // 在防守点内做小规模搜索（排序后再截断，修复 Bug1）
                std::vector<Cand> cs;
                for (int idx : def) cs.push_back({ idx, pointScore(b, idx, me) });
                std::sort(cs.begin(), cs.end(),
                          [](const Cand& a, const Cand& b2) { return a.score > b2.score; });
                if (cs.size() > 24) cs.resize(24);
                int bestVal = -kInf;
                Pos best = { cs.front().idx / n, cs.front().idx % n };
                int alpha = -kInf;
                for (const Cand& c : cs) {
                    if (timeUp()) break;
                    makeMove(b, c.idx, me);
                    int val;
                    if (judge.checkWin(b, { c.idx / n, c.idx % n }, me)) {
                        val = kWinScore;
                    } else {
                        val = -search(b, 4, -kInf, -alpha, opp, 1);   // 标准 PVS 镜像窗口
                    }
                    unmakeMove(b, c.idx);
                    if (val > bestVal) { bestVal = val; best = { c.idx / n, c.idx % n }; }
                    if (val > alpha) alpha = val;
                }
                return best;
            }
        }
        // 5) 己方 VCT 追胜
        {
            Pos v;
            if (vct(b, me, vctDepth, v)) return v;
        }
        // 6) 主搜索
        return searchBest(b, me);
    }
};

// ===========================================================================
// TacticalMax 对外接口
// ===========================================================================
TacticalMax::TacticalMax(Judge& judge) : impl_(std::make_unique<Impl>(judge)) {}
TacticalMax::~TacticalMax() = default;

bool TacticalMax::isHuman() const { return false; }
bool TacticalMax::needsDelay() const { return true; }
const char* TacticalMax::name() const { return "TacticalMax"; }

void TacticalMax::setTimeBudgetMs(int ms) {
    if (ms >= 100) impl_->budgetMs = ms;
}

Pos TacticalMax::chooseMove(Board& board, ChessType color) {
    return impl_->decide(board, color);
}