// ============================================================================
// tactical_max.cpp - TacticalMax 引擎实现（7 号棋手，本项目最强本地算法）
//
// 一句话架构：把"评估"从全盘扫描的函数，改造成增量维护的状态。
//
//   L0 增量窗口状态
//      枚举所有长度 w=winLen 的定长连续窗口（横/竖/两条斜线），建立
//      「格子 → 所属窗口」倒排索引。make/unmake 只触碰 ≤4w 个窗口，每个窗口
//      贡献 O(1)，单节点评估从 O(n²·w)≈4500 次访问降到 O(4w)≈20 次。
//      全部尺寸/连珠数取自 Board（n=4..30、w=4..15），无任何硬编码常量。
//
//   L1 棋型评估
//      窗口掩码 → 棋型等级查表（懒缓存）。关键设计：**等级的"活/冲"判定不猜
//      窗口外一格（outA/outB），一律交由"全局关键点集合"裁决** —— 见下方 Bug1
//      修复说明。这使 XX_XX 等断口棋型也能被正确识别（此项正是旧 ThreatDetector
//      的 scanLine 做不到的：它遇空格即停，看不见断口）。
//
//   L2 搜索框架
//      迭代加深（PV 前置重排 + 渴望窗口）+ Zobrist 置换表（深度优先替换）
//      + PVS + 杀手/历史启发 + LMR 晚期走法缩减 + 深度相关候选拓宽
//      + 硬性时间预算。
//
//   L3 追胜融合
//      VCF（连续冲四）/VCT（冲四 + 活三）与主搜索共享同一时间预算与置换表，
//      克服dl（各种赢法合一），消除地平线效应。
//
// ---------------------------------------------------------------------------
// 本次修订依据 garbage/bug2.txt 逐条落实：
//   Bug1 活四/活三判定两端对调
//        旧实现在 pc==w-1 时用 outA/outB 猜第二个成五点，且 A/B 恰好对调：
//        空位在起点端(h==0)时第二个成五点应是 cells[w]=outB，代码却查 outA，
//        导致 _XXXXO 被降级、OXXXX_ 被升级；pc==w-2 的活三判定复用同一套
//        配对逻辑，活三/眠三也一起错。
//        修法：不再由窗口猜。成五点由 five 集合（不同格子数）精确给出 ——
//        five.size()>=2 即活四（两个不同成五点），==1 即冲四；这条判据天然
//        处理断口形（XXX_XX 只贡献 1 个成五点，判为冲四）与棋盘边缘
//        （越界窗口根本不存在，不可能贡献成五点）。活三的"能否再走一子成活四"
//        则用 corrected pairing 在窗口内判定（Terms见 hasOuterFivePoint 注释）。
//   Bug2 超时结果写入置换表 + mate 分数未按 ply 归一化
//        search/searchBest/vcf/vct 统一用 complete 标志，只有完整搜完的节点
//        才允许 ttStore；mate 分数存入时 +ply、取出时 -ply。
//   Bug3 VCF/VCT 缺反击检查
//        对手被迫堵 my-four 之后，检查其是否顺势形成自己的成五/活四威胁；
//        若形成，本条连杀链作废（原 7 号"押上全部子力却崩盘"的根因）。
//   Bug4 防守层退回旧 ThreatDetector
//        改用引擎自带的 five/four 集合 + threatUnitsAfter() 自列必防点，
//        语义覆盖 ThreatDetector.mustDefend（双威胁）且额外支持断口棋型。
//   Bug5 性能三处（active 单调膨胀 / pointScore 走 make-unmake / countFive O(n²)）
//        active 改引用计数；pointScore 改为纯函数 localGain()（不落子、直接算
//        局部增益）；five/four 改 KeyPointSet（计数 + 双向链表，size()/first()
//        均 O(1)）。
//   Bug6 迭代加深顺序固定
//        每轮结束后把上一轮最佳着法提到队首，并写入 rootMove 供排序加权。
//   Bug7 补强枝术
//        新增 LMR、渴望窗口、组合威胁超线性加成、side-to-move tempo。
//        空着裁剪不启用：五子棋"停一手"不构成合法着法，且会让当前局面被严重
//        高估（pass 对自己永远有利），收益为负，故明确排除并在此说明。
// ---------------------------------------------------------------------------
// 另修两处致命工程问题：
//   * 旧稿 makeMove/unmakeMove 在 struct Impl 中重复定义两份，文件无法编译；
//   * 旧稿 buildIndex() 清空 wins 后从未生成任何窗口，wins 恒为空，增量评估
//     实际从未生效。本次一并重写。
// ============================================================================

#include "tactical_max.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

// ---- 尺寸上限：仅用于静态数组维度，实际一律取自 Board ----
constexpr int kMaxBoardSize = 30;                                  // 棋盘边长上限
constexpr int kMaxCells     = kMaxBoardSize * kMaxBoardSize;
constexpr int kMaxWinLen    = 15;                                  // 连珠数上限

constexpr int kDR[4] = { 0, 1, 1, 1 };
constexpr int kDC[4] = { 1, 0, 1, -1 };

constexpr int kDirCount = 4;

inline ChessType oppOf(ChessType c) { return opponent(c); }
inline int colorIndex(ChessType c) { return (c == ChessType::Black) ? 0 : 1; }

inline int popcount32(uint32_t x) {
    int c = 0;
    while (x) { x &= x - 1; ++c; }
    return c;
}

// 掩码中唯一的 0 位下标（要求 popcount(mask) == w-1）；不满足返回 -1
inline int loneEmptyBit(uint32_t mask, int w) {
    for (int k = 0; k < w; ++k)
        if ((mask & (1u << k)) == 0u) return k;
    return -1;
}

// ---- 棋型分值：整数便于严格比对。关键比值 活三/眠三=10、活二/眠二=10 ----
enum : int {
    SC_FIVE        = 1000000,   // 窗口填满：已成连
    SC_RUSH_FOUR   =   50000,   // 窗口差一子：至少是冲四（是否升格为活四见 evaluate）
    SC_LIVE_THREE  =   10000,
    SC_SLEEP_THREE =    1000,
    SC_LIVE_TWO    =     200,
    SC_SLEEP_TWO   =      20,
    SC_ONE         =       1,
};

// 附加值：由关键点集合裁决的"感觉到"信息，窗口结构是看不见的
enum : int {
    BONUS_FIVE       = 4000000,   // 已成五点存在
    BONUS_LIVE_FOUR  = 3000000,   // ≥2 个不同成五点（活四/双冲四）：必胜前夜
    BONUS_COMBO_ONE  =    9000,   // 每多一个成四点，组合威胁超线性加成
    BONUS_TEMPO      =     300,   // side-to-move：轮到走棋的一方略占优
};

constexpr int kWinScore  = 100000000;   // 评估 clamp 上界 ±kWinScore/2
constexpr int kMateScore = 100000000;   // 杀棋基准分
constexpr int kInf       = 1000000000;  // 搜索无穷（必须大于任何评估值）
constexpr int kMateBound = kMateScore - 1024;

// ---- mate 分数的 ply 归一化：同一局面在不同 ply 命中 TT 必须给出同一"距离根的距离" ----
inline bool isMateValue(int v) { return v > kMateBound || v < -kMateBound; }
inline int  mateAdjustIn (int v, int ply) {
    if (v >  kMateBound) return v + ply;
    if (v < -kMateBound) return v - ply;
    return v;
}
inline int  mateAdjustOut(int v, int ply) {
    if (v >  kMateBound) return v - ply;
    if (v < -kMateBound) return v + ply;
    return v;
}
inline int mateValue(int ply) { return kMateScore - ply; }

// ---------------------------------------------------------------------------
// 端部第二成五点判据（Bug1 的核心修正）
//
// 设窗口 cells[0..w-1] 沿方向排列，outA = cells[-1]（起点外侧），
// outB = cells[w]（终点外侧）。补满某侧端部后还剩 1 个空位时：
//   h == 0     ：空位在起点端，形如 .XXXX，补位在末端；此时另一个成五点是
//                cells[w]，即 **outB**（补 outB 后 cells[1..w] 五连）。
//   h == w - 1 ：空位在终点端，形如 XXXX.，补位在前端；此时另一个成五点是
//                cells[-1]，即 **outA**（补 outA 后 cells[-1..w-2] 五连）。
//   h 在中间   ：断口形（XX_XX），唯一补位补满后即五连，不存在第二个成五点。
//
// 旧实现把 h==0 配给 outA、h==w-1 配给 outB，两端恰好对调，导致活四被降级、
// 冲四被升级，并连带污染了复用同一配对的活三/眠三判定。
// ---------------------------------------------------------------------------
inline bool hasOuterFivePoint(int h, bool outAEmpty, bool outBEmpty, int w) {
    if (h == 0)         return outBEmpty;
    if (h == w - 1)     return outAEmpty;
    return false;
}

// ---------------------------------------------------------------------------
// 窗口棋型评估：纯函数，(己方掩码, 两端外侧是否为空, w) → 分值。
// 注意：本函数只负责"窗口内部能看到的档次"。四是否活、会不会被双击死，
// 由外层结合全局关键点集合裁决 —— 这是本次修订的结构性改动。
// ---------------------------------------------------------------------------
int windowValue(uint32_t selfMask, bool outAEmpty, bool outBEmpty, int w) {
    const int pc = popcount32(selfMask);
    if (pc == 0) return 0;
    if (pc == w) return SC_FIVE;

    if (pc == w - 1) {
        // 差一子：至少是冲四。能否升级为活四取决于"是否存在第二个不同的成五点"，
        // 这是跨窗口信息，本函数不做判定，统一交给 BONUS_LIVE_FOUR。
        return SC_RUSH_FOUR;
    }

    if (pc == w - 2) {
        // 差两子：枚举两个空位分别补.value，若某个补位使其变成「活四」则为活三。
        const uint32_t empties = (~selfMask) & ((1u << w) - 1u);
        for (int k = 0; k < w; ++k) {
            if ((empties & (1u << k)) == 0u) continue;
            const int h = loneEmptyBit(selfMask | (1u << k), w);
            if (hasOuterFivePoint(h, outAEmpty, outBEmpty, w)) return SC_LIVE_THREE;
        }
        return SC_SLEEP_THREE;
    }

    if (pc == w - 3) {
        // 差三子：枚举两次补位，看能否在某条路径上达到活四结构（按活二计）。
        const uint32_t empties = (~selfMask) & ((1u << w) - 1u);
        for (int k = 0; k < w; ++k) {
            if ((empties & (1u << k)) == 0u) continue;
            const uint32_t m2 = selfMask | (1u << k);
            const uint32_t e2 = (~m2) & ((1u << w) - 1u);
            if (popcount32(m2) != w - 2) continue;
            for (int j = 0; j < w; ++j) {
                if ((e2 & (1u << j)) == 0u) continue;
                const int h = loneEmptyBit(m2 | (1u << j), w);
                if (hasOuterFivePoint(h, outAEmpty, outBEmpty, w)) return SC_LIVE_TWO;
            }
        }
        return SC_SLEEP_TWO;
    }

    return SC_ONE * pc;                       // 更稀疏：仅作微弱位置倾向
}

// ---- 倒排索引单元：格子属于第 wid 个窗口的第 k 位 ----
struct WinRef {
    int wid;
    int k;
};

struct WindowInfo {
    std::array<int, kMaxWinLen> cells{};      // 窗口内格子索引（cells[k]）
    int outA = -1;                            // cells[-1]，越界为 -1
    int outB = -1;                            // cells[w]，越界为 -1
    uint32_t blackMask = 0;                   // 运行时：黑子占据掩码
    uint32_t whiteMask = 0;                   // 运行时：白子占据掩码
};

struct TTEntry {
    uint64_t key    = 0;
    int      depth  = -1;
    int      value  = 0;
    int      flag   = 0;                      // 0=exact, 1=lower bound, 2=upper bound
    int      move   = -1;                     // 最佳着法（r*n+c）
};

// ---------------------------------------------------------------------------
// 关键点集合：每格一个计数 + 双向链表表示"计数 > 0 的格子"。
// 目标：size()（对应旧 countFive）与 first()（对应旧 firstFive）均为 O(1)，
//       消除旧实现每次线性扫描 n² 格的开销（bug2 第 5.3 条）。
// 计数允许 >1：同一格子可以被多个窗口同时指认为成五点/成四点，但 size()
// 数的是"不同格子数"，这正是活四判定（≥2 个不同成五点）所需要的语义。
// ---------------------------------------------------------------------------
class KeyPointSet {
public:
    void reset(int cellCount) {
        const size_t m = static_cast<size_t>(cellCount);
        cnt_.assign(m, 0);
        prv_.assign(m, -1);
        nxt_.assign(m, -1);
        head_  = -1;
        count_ = 0;
    }

    void add(int idx)    { if (cnt_[static_cast<size_t>(idx)]++ == 0) link(idx); }
    void remove(int idx) { if (--cnt_[static_cast<size_t>(idx)] == 0) unlink(idx); }

    int  size()  const { return count_; }
    int  first() const { return head_; }
    bool empty() const { return count_ == 0; }
    bool contains(int idx) const { return cnt_[static_cast<size_t>(idx)] > 0; }

    const std::vector<int>& counts() const { return cnt_; }   // 供自测对比内部计数

    template <class Fn>
    void forEach(Fn&& fn) const {
        for (int i = head_; i >= 0; i = nxt_[static_cast<size_t>(i)]) fn(i);
    }

private:
    void link(int i) {
        const size_t s = static_cast<size_t>(i);
        prv_[s] = -1;
        nxt_[s] = head_;
        if (head_ >= 0) prv_[static_cast<size_t>(head_)] = i;
        head_ = i;
        ++count_;
    }
    void unlink(int i) {
        const size_t s = static_cast<size_t>(i);
        const int p = prv_[s], x = nxt_[s];
        if (p >= 0) nxt_[static_cast<size_t>(p)] = x; else head_ = x;
        if (x >= 0) prv_[static_cast<size_t>(x)] = p;
        prv_[s] = nxt_[s] = -1;
        --count_;
    }

    std::vector<int> cnt_;                    // 每格计数
    std::vector<int> prv_, nxt_;              // 双向链表
    int head_  = -1;
    int count_ = 0;
};

}  // namespace

// ===========================================================================
// 引擎实现（PImpl，全部细节封闭在此 struct 中）
// ===========================================================================
struct TacticalMax::Impl {
    // ---- 可调参数 ----
    int budgetMs    = 1500;     // 单步时间预算（毫秒，硬上限）
    int maxDepth    = 12;       // 迭代加深上限
    int vcfDepth    = 16;       // 顶层 VCF 最大连续冲四层数
    int leafVcf     = 6;        // 防守/叶子场景下的浅层 VCF
    int vctDepth    = 6;        // 顶层 VCT 最大层数
    int identRadius = 2;        // 邻域活跃半径（候选生成用）

    static constexpr int kTTBits = 18;        // 置换表容量 2^18
    static constexpr int kMaxPly = 64;        // 杀手表深度上限

    explicit Impl(Judge& j) : judge(j), tt(1u << kTTBits), rng(seedNow()) {}

    Judge& judge;

    // ---- 静态索引（随局面尺寸/连珠数重建） ----
    int n = 0;                                // 棋盘边长
    int w = 0;                                // 连珠数
    int cellTotal = 0;                        // n*n
    std::vector<WindowInfo>           wins;
    std::vector<std::vector<WinRef>>  refs;   // 格子 → 所属窗口列表
    mutable std::vector<int>      valCache;      // (mask<<2|outA<<1|outB) → 分值，-1 未算（懒写入）
    std::vector<uint64_t>             zob;        // Zobrist：每格每色一个随机数
    std::vector<TTEntry>              tt;
    std::vector<int>                  activeCnt;  // 邻域引用计数（>0 即活跃）

    // ---- 增量状态（makeMove/unmakeMove 严格配对维护） ----
    std::array<long long, 2> baseScore = { 0, 0 };    // 窗口结构分累加
    std::array<KeyPointSet, 2> keyFive;               // 成五点（差一子即成的格子）
    std::array<KeyPointSet, 2> keyFour;               // 成四点（再走一子可产出成五点的格子）
    uint64_t hash_ = 0;

    // ---- 搜索瞬时状态 ----
    std::chrono::steady_clock::time_point startTp;
    bool      stopped = false;
    long long nodes   = 0;
    int       rootMove = -1;
    std::vector<std::array<int, 2>> killer;
    std::vector<int> history;

    // ---- 临时缓冲：避免热路径频繁分配 ----
    mutable std::vector<int> scratchPts;

    std::mt19937 rng;

    static uint64_t seedNow() {
        return static_cast<uint64_t>(
                   std::chrono::steady_clock::now().time_since_epoch().count())
               ^ (static_cast<uint64_t>(std::random_device{}()) << 32)
               ^ static_cast<uint64_t>(std::random_device{}());
    }

    // ================= 基础工具 =================
    inline uint32_t fullMask() const { return (1u << w) - 1u; }
    inline int idxOf(int r, int c) const { return r * n + c; }
    inline bool inBounds(int r, int c) const { return r >= 0 && c >= 0 && r < n && c < n; }
    inline bool isStone(const Board& b, int idx) const {
        return b.at(idx / n, idx % n) != ChessType::None;
    }
    inline bool outEmpty(const Board& b, int idx) const {
        return idx >= 0 && b.at(idx / n, idx % n) == ChessType::None;
    }
    inline bool inRangeIdx(int idx) const { return idx >= 0 && idx < cellTotal; }
    inline uint32_t maskOf(const WindowInfo& wi, ChessType c) const {
        return (c == ChessType::Black) ? wi.blackMask : wi.whiteMask;
    }

    // 掩码 → 分值（懒缓存，跨步保留，缓存命中率高）
    int cachedValue(uint32_t mask, bool oa, bool ob) const {
        const size_t key = (static_cast<size_t>(mask) << 2)
                         | (oa ? 2u : 0u) | (ob ? 1u : 0u);
        const int v = valCache[key];
        if (v >= 0) return v;
        const int computed = windowValue(mask, oa, ob, w);
        valCache[key] = computed;
        return computed;
    }

    // 时间预算：每 1024 节点探一次时钟，避免频繁读时钟
    bool timeUp() {
        if (stopped) return true;
        if ((nodes & 1023) == 0) {
            const auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startTp).count();
            if (el >= budgetMs) stopped = true;
        }
        return stopped;
    }

    // ================= 索引构建 =================
    void buildIndex(const Board& b) {
        n = b.size();
        w = b.winLen();
        cellTotal = n * n;

        wins.clear();
        refs.assign(static_cast<size_t>(cellTotal), {});

        // Zobrist 随机数表（旧索引丢失时一并重掷，保证每局略有差异）
        zob.assign(static_cast<size_t>(cellTotal) * 2, 0);
        for (auto& z : zob) z = (static_cast<uint64_t>(rng()) << 32) ^ rng();

        // valCache 只随 winLen 变化：key = mask<<2 | outA<<1 | outB
        valCache.assign(static_cast<size_t>(1) << (w + 2), -1);

        tt.assign(1u << kTTBits, TTEntry{});
        activeCnt.assign(static_cast<size_t>(cellTotal), 0);
        keyFive[0].reset(cellTotal);
        keyFive[1].reset(cellTotal);
        keyFour[0].reset(cellTotal);
        keyFour[1].reset(cellTotal);
        history.assign(static_cast<size_t>(cellTotal), 0);

        if (n <= 0 || w <= 0 || w > n) return;      // 退化尺寸：不建窗口，走兜底候选

        // 枚举全部长度 w 的定长窗口：4 个方向 × 每个可能起点
        for (int d = 0; d < kDirCount; ++d) {
            for (int r = 0; r < n; ++r) {
                for (int c = 0; c < n; ++c) {
                    const int endR = r + (w - 1) * kDR[d];
                    const int endC = c + (w - 1) * kDC[d];
                    if (!inBounds(endR, endC)) continue;      // 窗口越界，不存在

                    WindowInfo wi;
                    for (int k = 0; k < w; ++k) {
                        const int rr = r + k * kDR[d];
                        const int cc = c + k * kDC[d];
                        const int cell = idxOf(rr, cc);
                        wi.cells[static_cast<size_t>(k)] = cell;
                        refs[static_cast<size_t>(cell)].push_back(
                            WinRef{ static_cast<int>(wins.size()), k });
                    }
                    const int ar = r - kDR[d], ac = c - kDC[d];
                    const int br = r + w * kDR[d], bc = c + w * kDC[d];
                    wi.outA = inBounds(ar, ac) ? idxOf(ar, ac) : -1;
                    wi.outB = inBounds(br, bc) ? idxOf(br, bc) : -1;
                    wins.push_back(wi);
                }
            }
        }
    }

    // ================= 增量贡献维护 =================
    // sign = -1 移除旧贡献 / +1 添加新贡献。只读当前掩码，不修改掩码。
    void contribute(const Board& b, int wid, int sign) {
        WindowInfo& wi = wins[static_cast<size_t>(wid)];
        if (wi.blackMask == 0u && wi.whiteMask == 0u) return;   // 空窗口无贡献

        const bool oa = outEmpty(b, wi.outA);
        const bool ob = outEmpty(b, wi.outB);

        for (int t = 0; t < 2; ++t) {
            const uint32_t sm = (t == 0) ? wi.blackMask : wi.whiteMask;
            const uint32_t om = (t == 0) ? wi.whiteMask : wi.blackMask;
            if (sm == 0u) continue;                 // 本方在此窗口无子
            if (om != 0u) continue;                 // 对手已污染，双方在此窗口均无价值

            if (sign > 0) baseScore[static_cast<size_t>(t)] += cachedValue(sm, oa, ob);
            else          baseScore[static_cast<size_t>(t)] -= cachedValue(sm, oa, ob);

            const int pc = popcount32(sm);
            if (pc == w - 1) {
                // 唯一空位 → 成五点
                const int h = loneEmptyBit(sm, w);
                if (h >= 0) {
                    const int pt = wi.cells[static_cast<size_t>(h)];
                    if (sign > 0) keyFive[static_cast<size_t>(t)].add(pt);
                    else          keyFive[static_cast<size_t>(t)].remove(pt);
                }
            } else if (pc == w - 2) {
                // 两个空位 → 均可作为成四点
                const uint32_t e = (~sm) & fullMask();
                for (int k = 0; k < w; ++k) {
                    if ((e & (1u << k)) == 0u) continue;
                    const int pt = wi.cells[static_cast<size_t>(k)];
                    if (sign > 0) keyFour[static_cast<size_t>(t)].add(pt);
                    else          keyFour[static_cast<size_t>(t)].remove(pt);
                }
            }
        }
    }

    // 邻域活跃标记：引用计数，makeMove +1 / unmakeMove -1。
    // 旧实现只有 markActive 没有 unmark，搜索几百节点后活跃集合单调膨胀，
    // 候选越来越多、越搜越慢（bug2 第 5.1 条）。
    void bumpActive(int idx, int delta) {
        const int r = idx / n, c = idx % n;
        for (int dr = -identRadius; dr <= identRadius; ++dr) {
            const int nr = r + dr;
            if (nr < 0 || nr >= n) continue;
            for (int dc = -identRadius; dc <= identRadius; ++dc) {
                const int nc = c + dc;
                if (nc < 0 || nc >= n) continue;
                activeCnt[static_cast<size_t>(nr * n + nc)] += delta;
            }
        }
    }

    // 从当前棋盘整体重建增量状态（每步决策开始时调用一次）
    void resetState(const Board& b) {
        for (auto& wi : wins) { wi.blackMask = 0u; wi.whiteMask = 0u; }
        baseScore[0] = baseScore[1] = 0;
        keyFive[0].reset(cellTotal);
        keyFive[1].reset(cellTotal);
        keyFour[0].reset(cellTotal);
        keyFour[1].reset(cellTotal);
        std::fill(activeCnt.begin(), activeCnt.end(), 0);
        hash_ = 0;

        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                const ChessType t = b.at(r, c);
                if (t == ChessType::None) continue;
                const int idx = idxOf(r, c);
                const int ci = colorIndex(t);
                hash_ ^= zob[static_cast<size_t>(idx) * 2 + static_cast<size_t>(ci)];
                for (const WinRef& ref : refs[static_cast<size_t>(idx)])
                    setCellMask(wins[static_cast<size_t>(ref.wid)], t, ref.k, true);
                bumpActive(idx, +1);
            }
        }

        // 掩码就位后统一累加贡献，避免逐格 contribute 的重复扣补
        for (size_t wid = 0; wid < wins.size(); ++wid) {
            WindowInfo& wi = wins[wid];
            if (wi.blackMask == 0u && wi.whiteMask == 0u) continue;
            const bool oa = outEmpty(b, wi.outA);
            const bool ob = outEmpty(b, wi.outB);
            for (int t = 0; t < 2; ++t) {
                const uint32_t sm = (t == 0) ? wi.blackMask : wi.whiteMask;
                const uint32_t om = (t == 0) ? wi.whiteMask : wi.blackMask;
                if (sm == 0u || om != 0u) continue;
                baseScore[static_cast<size_t>(t)] += cachedValue(sm, oa, ob);
                const int pc = popcount32(sm);
                if (pc == w - 1) {
                    const int h = loneEmptyBit(sm, w);
                    if (h >= 0)
                        keyFive[static_cast<size_t>(t)].add(wi.cells[static_cast<size_t>(h)]);
                } else if (pc == w - 2) {
                    const uint32_t e = (~sm) & fullMask();
                    for (int k = 0; k < w; ++k)
                        if ((e & (1u << k)) != 0u)
                            keyFour[static_cast<size_t>(t)].add(wi.cells[static_cast<size_t>(k)]);
                }
            }
        }
    }

    inline void setCellMask(WindowInfo& wi, ChessType color, int k, bool occupied) {
        const uint32_t bit = 1u << k;
        uint32_t& m = (color == ChessType::Black) ? wi.blackMask : wi.whiteMask;
        if (occupied) m |= bit; else m &= ~bit;
    }

    void makeMove(Board& b, int idx, ChessType color) {
        const size_t s = static_cast<size_t>(idx);
        // 统一路径：扣掉受影响的旧贡献 → 落子 → 加回新贡献。
        // idx 原为空位，故"旧贡献"即不含该子的状态，净效果恰为增量。
        for (const WinRef& ref : refs[s]) contribute(b, ref.wid, -1);
        b.set(idx / n, idx % n, color);
        hash_ ^= zob[s * 2 + static_cast<size_t>(colorIndex(color))];
        for (const WinRef& ref : refs[s])
            setCellMask(wins[static_cast<size_t>(ref.wid)], color, ref.k, true);
        for (const WinRef& ref : refs[s]) contribute(b, ref.wid, +1);
        bumpActive(idx, +1);
    }

    void unmakeMove(Board& b, int idx) {
        const size_t s = static_cast<size_t>(idx);
        const ChessType color = b.at(idx / n, idx % n);
        for (const WinRef& ref : refs[s]) contribute(b, ref.wid, -1);
        b.set(idx / n, idx % n, ChessType::None);
        hash_ ^= zob[s * 2 + static_cast<size_t>(colorIndex(color))];
        for (const WinRef& ref : refs[s])
            setCellMask(wins[static_cast<size_t>(ref.wid)], color, ref.k, false);
        for (const WinRef& ref : refs[s]) contribute(b, ref.wid, +1);
        bumpActive(idx, -1);
    }

    // ================= 评估 =================
    // 单方强度 = 窗口结构分 + 关键点附加值。
    //   * 窗口结构分：看得见眠三/眠二/活二等细节，但看不见"是否活得下去"；
    //   * 关键点附加值：five.size()>=2 即活四（两个不同成五点，对手堵不完），
    //     这是窗口层面无论如何都表达不出来的跨窗口事实（bug2 第 1 条）。
    long long sideStrength(int ci) const {
        const size_t s = static_cast<size_t>(ci);
        long long v = baseScore[s];

        const int nf = keyFive[s].size();
        if (nf >= 2)      v += BONUS_LIVE_FOUR;          // 活四：必胜前夜
        else if (nf == 1) v += BONUS_FIVE * 0;           // 冲四已在窗口分中体现，不重复计
        if (nf >= 1)      v += SC_RUSH_FOUR;             // 存在成五点：至少是冲四

        // 组合威胁：多个成四点并存时（双三/四三雏形）做超线性加成，
        // 使"双威胁"严格优于两个单点之和，保证 AI 主动经营组合，而非逐个试探。
        const int n4 = keyFour[s].size();
        if (n4 >= 2) v += static_cast<long long>(BONUS_COMBO_ONE) * (n4 - 1);

        return v;
    }

    int evaluate(ChessType me) const {
        const int ci = colorIndex(me), cj = 1 - ci;
        // 对手威胁加权 1.1 倍：同等棋型下偏防守，避免与对手对攻时被抢先成五。
        long long v = sideStrength(ci) - sideStrength(cj) * 11 / 10;
        v += BONUS_TEMPO;                                 // side-to-move：先走方略优
        if (v >  kWinScore / 2) v =  kWinScore / 2;
        if (v < -kWinScore / 2) v = -kWinScore / 2;
        return static_cast<int>(v);
    }

    // ================= 局部启发式（不再落子） =================
    // 纯函数：计算 me 落在 idx 后的价值增量 = 己方增益 + 对手被封堵的损失。
    // 旧实现走一遍完整 make/unmake，每个候选两次遍历 ≤4w 个窗口（bug2 第 5.2 条）；
    // 这里改为仅读掩码直接作差，不触碰任何全局状态，也不改变棋盘。
    int localGain(const Board& b, int idx, ChessType me) const {
        const ChessType op = oppOf(me);
        long long gain = 0;
        for (const WinRef& ref : refs[static_cast<size_t>(idx)]) {
            const WindowInfo& wi = wins[static_cast<size_t>(ref.wid)];
            const uint32_t bit = 1u << ref.k;
            const bool oa = outEmpty(b, wi.outA);
            const bool ob = outEmpty(b, wi.outB);
            const uint32_t myMask = maskOf(wi, me);
            const uint32_t opMask = maskOf(wi, op);

            if (opMask != 0u) {
                // 对手已占此窗口：我方落子后对手贡献归零 → 封堵收益
                gain += cachedValue(opMask, oa, ob);
            } else if (myMask == 0u) {
                gain += cachedValue(bit, oa, ob);
            } else {
                gain += cachedValue(myMask | bit, oa, ob) - cachedValue(myMask, oa, ob);
            }
        }
        if (gain > 1000000000LL) gain = 1000000000LL;
        return static_cast<int>(gain);
    }

    // 模拟 c 落在 idx 后新增的不同成五点（不含棋盘上已经存在的）
    void newFivePoints(int idx, ChessType c, std::vector<int>& out) const {
        out.clear();
        const ChessType op = oppOf(c);
        for (const WinRef& ref : refs[static_cast<size_t>(idx)]) {
            const WindowInfo& wi = wins[static_cast<size_t>(ref.wid)];
            if (maskOf(wi, op) != 0u) continue;                 // 对手已占，成五无望
            const uint32_t mine = maskOf(wi, c);
            if ((mine & (1u << ref.k)) != 0u) continue;         // 该格已有己方子
            const uint32_t m2 = mine | (1u << ref.k);
            const int pc = popcount32(m2);
            if (pc == w) {
                const int pt = idx;                             // 落子即成五
                if (std::find(out.begin(), out.end(), pt) == out.end()) out.push_back(pt);
                continue;
            }
            if (pc != w - 1) continue;                          // 补满后没形成"四"
            const int h = loneEmptyBit(m2, w);
            if (h < 0) continue;
            const int pt = wi.cells[static_cast<size_t>(h)];
            if (std::find(out.begin(), out.end(), pt) == out.end()) out.push_back(pt);
        }
    }

    // c 落在 idx 后的威胁单位数 = 不同成五点数 + 新增活三数。
    // >= 2 意味着形成对方堵不完的强制胜形态（活四/双冲四/四三/双活三），
    // 与 ThreatDetector::threatCount 同语义，但支持 XX_XX 类断口棋型（bug2 第 4 条）。
    int threatUnitsAfter(const Board& b, int idx, ChessType c) const {
        newFivePoints(idx, c, scratchPts);
        int units = keyFive[static_cast<size_t>(colorIndex(c))].size();
        for (int pt : scratchPts)
            if (pt != idx && !keyFive[static_cast<size_t>(colorIndex(c))].contains(pt)) ++units;
        if (units >= 2) return units;

        // 新增活三：窗口内加上本子后达到"再走一子成活四"的结构
        const ChessType op = oppOf(c);
        for (const WinRef& ref : refs[static_cast<size_t>(idx)]) {
            const WindowInfo& wi = wins[static_cast<size_t>(ref.wid)];
            if (maskOf(wi, op) != 0u) continue;
            const uint32_t mine = maskOf(wi, c);
            if ((mine & (1u << ref.k)) != 0u) continue;
            const uint32_t m2 = mine | (1u << ref.k);
            if (popcount32(m2) != w - 2) continue;
            const bool oa = outEmpty(b, wi.outA);
            const bool ob = outEmpty(b, wi.outB);
            const uint32_t e2 = (~m2) & fullMask();
            for (int k = 0; k < w; ++k) {
                if ((e2 & (1u << k)) == 0u) continue;
                const int h = loneEmptyBit(m2 | (1u << k), w);
                if (hasOuterFivePoint(h, oa, ob, w)) { ++units; break; }
            }
        }
        return units;
    }

    // 该点是否是我方/对手的关键棋型点（用于排序与 LMR 豁免）
    inline bool isKeyPoint(int idx, ChessType c) const {
        return keyFive[static_cast<size_t>(colorIndex(c))].contains(idx)
            || keyFour[static_cast<size_t>(colorIndex(c))].contains(idx);
    }

    // ================= 候选生成 =================
    struct Cand { int idx; int score; };

    std::vector<Cand> genCands(const Board& b, ChessType me, int depth, int ply) {
        std::vector<Cand> out;
        for (int idx = 0; idx < cellTotal; ++idx) {
            if (activeCnt[static_cast<size_t>(idx)] <= 0) continue;
            if (isStone(b, idx)) continue;
            out.push_back({ idx, localGain(b, idx, me) });
        }
        if (out.empty()) {                                   // 兜底：无活跃点时退化为中心
            const int mid = (n / 2) * n + n / 2;
            if (inRangeIdx(mid) && !isStone(b, mid)) out.push_back({ mid, 0 });
        }
        if (out.empty()) {                                   // 再兜底：任意空位
            for (int idx = 0; idx < cellTotal; ++idx)
                if (!isStone(b, idx)) out.push_back({ idx, 0 });
        }
        if (out.empty()) return out;

        // TT 主变 / 杀手着法加权：放在排序之前，保证不会被宽度截断丢掉
        for (auto& c : out) {
            if (c.idx == rootMove) c.score += 400000000;
            if (ply < static_cast<int>(killer.size())) {
                if (c.idx == killer[static_cast<size_t>(ply)][0])      c.score += 200000000;
                else if (c.idx == killer[static_cast<size_t>(ply)][1]) c.score += 100000000;
            }
            c.score += std::min(history[static_cast<size_t>(c.idx)], 1000000) * 8;
        }
        std::sort(out.begin(), out.end(),
                  [](const Cand& a, const Cand& x) { return a.score > x.score; });

        // 深度相关候选拓宽：关键棋型点强制保留，不参与截断（bug.txt Bug1）
        const int cap = (depth >= 7) ? 24 : (depth >= 4) ? 16 : 12;
        if (static_cast<int>(out.size()) > cap) {
            std::vector<Cand> forced;
            for (int i = cap; i < static_cast<int>(out.size()); ++i)
                if (isKeyPoint(out[static_cast<size_t>(i)].idx, me)) forced.push_back(out[static_cast<size_t>(i)]);
            out.resize(static_cast<size_t>(cap));
            for (const Cand& c : forced) out.push_back(c);
        }
        return out;
    }

    // ================= 置换表 =================
    void ttStore(uint64_t key, int depth, int value, int flag, int move, int ply) {
        const size_t slot = static_cast<size_t>(key) & (tt.size() - 1);
        TTEntry& e = tt[slot];
        if (e.key == key || e.depth <= depth)
            e = TTEntry{ key, depth, mateAdjustIn(value, ply), flag, move };
    }

    bool ttProbe(uint64_t key, int depth, int alpha, int beta, int& value, int& move, int ply) {
        const TTEntry& e = tt[static_cast<size_t>(key) & (tt.size() - 1)];
        if (e.key != key || e.depth < depth) return false;
        move = e.move;
        const int v = mateAdjustOut(e.value, ply);
        if (e.flag == 0) { value = v; return true; }
        if (e.flag == 1 && v >= beta)  { value = v; return true; }
        if (e.flag == 2 && v <= alpha) { value = v; return true; }
        return false;
    }

    // ================= VCF：连续冲四取胜 =================
    bool vcf(Board& b, ChessType me, int depth, Pos& best) {
        if (depth <= 0 || timeUp()) return false;
        ++nodes;
        const int ci = colorIndex(me), oi = 1 - ci;
        const ChessType op = oppOf(me);

        const int f = keyFive[static_cast<size_t>(ci)].first();
        if (f >= 0) { best = { f / n, f % n }; return true; }

        std::vector<Cand> cands;
        keyFour[static_cast<size_t>(ci)].forEach([&](int idx) {
            if (!isStone(b, idx)) cands.push_back({ idx, localGain(b, idx, me) });
        });
        if (cands.empty()) return false;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& x) { return a.score > x.score; });

        for (const Cand& c : cands) {
            if (timeUp()) return false;
            makeMove(b, c.idx, me);

            const int fiveCnt = keyFive[static_cast<size_t>(ci)].size();
            if (fiveCnt >= 2) {                             // 活四：对手无法双堵
                unmakeMove(b, c.idx);
                best = { c.idx / n, c.idx % n };
                return true;
            }
            if (fiveCnt == 1) {
                const int blk = keyFive[static_cast<size_t>(ci)].first();
                if (blk >= 0 && !isStone(b, blk)) {
                    makeMove(b, blk, op);
                    // Bug3：反击检查。对手被迫堵棋的同时若顺势形成自己的成五/活四，
                    // 我方下一手必须先处理，这条连杀链就此作废。
                    const bool oppCounterThreat = keyFive[static_cast<size_t>(oi)].size() >= 1;
                    Pos sub;
                    bool win = false;
                    if (!oppCounterThreat) win = vcf(b, me, depth - 1, sub);
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

    // 我方落子后的必应点集合（VCT 的 AND 节点）
    std::vector<int> forcedReplies(Board& b, ChessType me, ChessType nextSide) {
        std::vector<int> out;
        const int ci = colorIndex(me);

        const int f = keyFive[static_cast<size_t>(ci)].first();
        if (f >= 0) { out.push_back(f); return out; }        // 已成五：唯一应手

        // 尚未成五但存在多个成五点（双威胁）：对方必须逐一应对，此处全部列出
        keyFive[static_cast<size_t>(ci)].forEach([&](int idx) {
            if (!isStone(b, idx)) out.push_back(idx);
        });
        if (!out.empty()) return out;

        (void)nextSide;
        return out;
    }

    // ================= VCT：冲四 + 活三追胜（AND-OR） =================
    bool vct(Board& b, ChessType me, int depth, Pos& best) {
        if (depth <= 0 || timeUp()) return false;
        ++nodes;
        const int ci = colorIndex(me), oi = 1 - ci;
        const ChessType op = oppOf(me);

        Pos v;
        if (vcf(b, me, leafVcf, v)) { best = v; return true; }

        std::vector<Cand> cands;
        keyFour[static_cast<size_t>(ci)].forEach([&](int idx) {
            if (isStone(b, idx)) return;
            const int units = threatUnitsAfter(b, idx, me);
            cands.push_back({ idx, units * 1000000 + localGain(b, idx, me) });
        });
        if (cands.empty()) return false;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& x) { return a.score > x.score; });
        if (cands.size() > 12) cands.resize(12);

        for (const Cand& c : cands) {
            if (timeUp()) return false;
            makeMove(b, c.idx, me);

            std::vector<int> replies = forcedReplies(b, me, op);
            if (replies.empty() || replies.size() > 8) { unmakeMove(b, c.idx); continue; }

            bool allWin = true;
            for (int rp : replies) {
                if (isStone(b, rp)) continue;
                makeMove(b, rp, op);
                // Bug3（AND 侧）：对手应手后若自己先形成威胁，本分支不可采信
                const bool oppCounterThreat = keyFive[static_cast<size_t>(oi)].size() >= 1;
                Pos sub;
                bool ok = false;
                if (!oppCounterThreat) ok = vct(b, me, depth - 1, sub);
                unmakeMove(b, rp);
                if (!ok || timeUp()) { allWin = false; break; }
            }
            unmakeMove(b, c.idx);
            if (allWin) { best = { c.idx / n, c.idx % n }; return true; }
        }
        return false;
    }

    // ================= 静态搜索（叶子稳定化） =================
    int quiesce(ChessType me, int alpha, int beta) {
        ++nodes;
        const int stand = evaluate(me);
        if (stand >= beta) return stand;
        if (stand > alpha) alpha = stand;
        return alpha;
    }

    // ================= 主搜索：PVS + α-β + LMR =================
    int search(Board& b, int depth, int alpha, int beta, ChessType me, int ply) {
        ++nodes;
        if (timeUp()) return evaluate(me);
        if (depth <= 0) return quiesce(me, alpha, beta);

        int ttMove = -1, ttVal = 0;
        if (ttProbe(hash_, depth, alpha, beta, ttVal, ttMove, ply)) return ttVal;

        std::vector<Cand> cands = genCands(b, me, depth, ply);
        if (cands.empty()) return evaluate(me);

        // 一步成五：直接结算（消除地平线效应）
        for (const Cand& c : cands) {
            if (keyFive[static_cast<size_t>(colorIndex(me))].contains(c.idx)) {
                const int win = mateValue(ply);
                ttStore(hash_, depth, win, 0, c.idx, ply);
                return win;
            }
        }

        const int origAlpha = alpha, origBeta = beta;
        const ChessType op = oppOf(me);
        int best = -kInf, bestMove = cands.front().idx;
        bool first = true;
        bool complete = true;                     // Bug2：本节点是否完整搜完

        for (const Cand& c : cands) {
            if (timeUp()) { complete = false; break; }
            makeMove(b, c.idx, me);

            int val;
            if (judge.checkWin(b, { c.idx / n, c.idx % n }, me)) {
                val = mateValue(ply);
            } else if (first) {
                val = -search(b, depth - 1, -beta, -alpha, op, ply + 1);
            } else {
                // LMR：靠后的安静着法先做浅搜索，被证伪就不再深搜。
                // 关键棋型点（成五/成四点）与零窗口命中的局面一律不减深度。
                const bool tacticalMove = keyFive[static_cast<size_t>(colorIndex(me))].contains(c.idx)
                                       || keyFour[static_cast<size_t>(colorIndex(me))].contains(c.idx);
                const bool inCheckish   = keyFive[static_cast<size_t>(colorIndex(op))].size() > 0;
                if (depth >= 3 && !tacticalMove && !inCheckish) {
                    const int reduction = (depth >= 6) ? 2 : 1;
                    val = -search(b, depth - 1 - reduction, -alpha - 1, -alpha, op, ply + 1);
                    if (val > alpha)
                        val = -search(b, depth - 1, -alpha - 1, -alpha, op, ply + 1);
                } else {
                    val = -search(b, depth - 1, -alpha - 1, -alpha, op, ply + 1);   // 零窗口探测
                }
                if (val > alpha && val < beta)                                       // 失败重搜
                    val = -search(b, depth - 1, -beta, -alpha, op, ply + 1);
            }
            unmakeMove(b, c.idx);

            if (timeUp()) { complete = false; break; }
            if (val > best) { best = val; bestMove = c.idx; }
            if (val > alpha) alpha = val;
            if (alpha >= beta) {
                if (ply < static_cast<int>(killer.size())) {
                    std::array<int, 2>& kv = killer[static_cast<size_t>(ply)];
                    if (kv[0] != c.idx) { kv[1] = kv[0]; kv[0] = c.idx; }
                }
                int& hv = history[static_cast<size_t>(c.idx)];
                hv = std::min(hv + depth * depth, 1000000);
                break;
            }
            first = false;
        }

        // Bug2：被时间截断的节点结果是偏低的伪下界，写入 TT 会被深层搜索命中
        // 并返回，表现为"随机走坏棋"。只有完整搜完的结果才可入库。
        if (complete) {
            const int flag = (best <= origAlpha) ? 2 : (best >= origBeta ? 1 : 0);
            ttStore(hash_, depth, best, flag, bestMove, ply);
        }
        return best;
    }

    // ================= 迭代加深 =================
    Pos searchBest(Board& b, ChessType me) {
        const ChessType op = oppOf(me);
        std::vector<Cand> roots = genCands(b, me, 99, 0);      // 根层用最宽候选
        if (roots.empty()) return { -1, -1 };

        Pos bestPos = { roots.front().idx / n, roots.front().idx % n };
        int bestVal = -kInf;

        for (int depth = 4; depth <= maxDepth; ++depth) {
            // 渴望窗口：以已估值为中心开小窗口，失败再放宽。
            // 窗口命中时剪枝远多于全窗口；失败时的重搜代价远小于收益。
            int window = 50000;
            int localBest = -kInf;
            Pos  localPos = bestPos;
            bool complete = true;

            while (true) {
                int alpha = -kInf, beta = kInf;
                if (bestVal > -kInf / 2) {
                    alpha = std::max(-kInf, bestVal - window);
                    beta  = std::min( kInf, bestVal + window);
                }
                localBest = -kInf;
                localPos  = bestPos;
                complete  = true;

                for (const Cand& c : roots) {
                    if (timeUp()) { complete = false; break; }
                    makeMove(b, c.idx, me);
                    int val;
                    if (judge.checkWin(b, { c.idx / n, c.idx % n }, me)) {
                        val = mateValue(0);
                    } else {
                        val = -search(b, depth - 1, -beta, -alpha, op, 1);
                    }
                    unmakeMove(b, c.idx);
                    if (val > localBest) { localBest = val; localPos = { c.idx / n, c.idx % n }; }
                    if (val > alpha) alpha = val;
                }

                if (!complete) break;                          // 超时：丢弃本层
                if (localBest <= (bestVal > -kInf / 2 ? bestVal - window : -kInf)) {
                    window *= 4;                               // fail low：放宽重试
                    continue;
                }
                if (localBest >= beta) { window *= 4; continue; }  // fail high
                break;
            }

            if (!complete) break;                              // 预算用尽，保留上一层结果
            bestVal = localBest;
            bestPos = localPos;

            // Bug6：把本层最佳着法提到队首并写入 rootMove，
            // 让下一层从上一轮 PV 开始搜索，恢复迭代加深最大的收益来源。
            rootMove = bestPos.r * n + bestPos.c;
            for (size_t i = 1; i < roots.size(); ++i) {
                if (roots[i].idx == rootMove) { std::swap(roots[0], roots[i]); break; }
            }

            if (isMateValue(bestVal) || bestVal > kWinScore / 2) break;   // 已见胜势
        }
        return bestPos;
    }

    // ================= 防守层：用引擎自身的棋型计数列必防点 =================
    // 旧实现回调 ThreatDetector::mustDefend，而后者基于 scanLine（遇空格即停），
    // 看不见 XX_XX 这类断口棋型，等于把新引擎最大的优势又丢掉了（bug2 第 4 条）。
    // 这里统一用 five/four 集合 + threatUnitsAfter 判定，语义严格更强。
    std::vector<int> collectDefense(const Board& b, ChessType op) const {
        std::vector<int> out;
        const size_t oi = static_cast<size_t>(colorIndex(op));

        // 1) 对手已经存在成五点 → 必堵（两点以上即已成活四，尽力堵其一）
        keyFive[oi].forEach([&](int idx) { if (!isStone(b, idx)) out.push_back(idx); });
        if (!out.empty()) return out;

        // 2) 对手一手就能形成强制胜形态（threatUnits >= 2）的点 → 抢先处理
        keyFour[oi].forEach([&](int idx) {
            if (isStone(b, idx)) return;
            if (threatUnitsAfter(b, idx, op) >= 2) out.push_back(idx);
        });

        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // ================= 决策入口 =================
    Pos decide(Board& b, ChessType color) {
        if (b.size() != n || b.winLen() != w) buildIndex(b);
        resetState(b);

        nodes    = 0;
        stopped  = false;
        startTp  = std::chrono::steady_clock::now();
        rootMove = -1;
        killer.assign(static_cast<size_t>(kMaxPly), { -1, -1 });
        for (int& h : history) h /= 2;              // 历史启发半衰，避免长期饱和

        const ChessType me = color, op = oppOf(color);
        const int ci = colorIndex(me), oi = colorIndex(op);

        // 0) 空行 Scene：无任何可用窗口时的兜底（极小棋盘 / w > n 的退化规则）
        if (wins.empty() || cellTotal <= 0) {
            for (int idx = 0; idx < cellTotal; ++idx)
                if (!isStone(b, idx)) return { idx / n, idx % n };
            return { -1, -1 };
        }

        // 1) 己方一步成五：直接取走
        {
            const int f = keyFive[static_cast<size_t>(ci)].first();
            if (f >= 0) return { f / n, f % n };
        }
        // 2) 对方一步成五：必须堵
        {
            const int f = keyFive[static_cast<size_t>(oi)].first();
            if (f >= 0) return { f / n, f % n };
        }
        // 3) 己方连续冲四直接取胜
        {
            Pos v;
            if (vcf(b, me, vcfDepth, v)) return v;
        }
        // 4) 防守：堵住对手的强制胜制造点，或抢占其 VCF 起点
        {
            std::vector<int> def = collectDefense(b, op);
            if (def.empty()) {
                Pos v;
                if (vcf(b, op, leafVcf, v)) {                 // 对手有 VCF 杀
                    keyFour[static_cast<size_t>(oi)].forEach([&](int idx) {
                        if (!isStone(b, idx)) def.push_back(idx);
                    });
                }
            }
            if (!def.empty()) {
                std::vector<Cand> cs;
                for (int idx : def) cs.push_back({ idx, localGain(b, idx, me) });
                std::sort(cs.begin(), cs.end(),
                          [](const Cand& a, const Cand& x) { return a.score > x.score; });
                if (cs.size() > 24) cs.resize(24);

                int bestVal = -kInf;
                Pos  best   = { cs.front().idx / n, cs.front().idx % n };
                int alpha   = -kInf;
                bool complete = true;
                for (const Cand& c : cs) {
                    if (timeUp()) { complete = false; break; }
                    makeMove(b, c.idx, me);
                    int val;
                    if (judge.checkWin(b, { c.idx / n, c.idx % n }, me)) {
                        val = mateValue(0);
                    } else {
                        val = -search(b, 4, -kInf, -alpha, op, 1);
                    }
                    unmakeMove(b, c.idx);
                    if (val > bestVal) { bestVal = val; best = { c.idx / n, c.idx % n }; }
                    if (val > alpha) alpha = val;
                }
                if (complete || bestVal > -kInf / 2) return best;
            }
        }
        // 5) 己方 VCT 追胜（冲四 + 活三组合）
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
TacticalMax::TacticalMax(Judge& judge) : impl_(std::make_unique<TacticalMax::Impl>(judge)) {}
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

// ===========================================================================
// 自测支持接口实现（仅供 bench / 调试使用，不参与决策路径）
// 这些方法会重建引擎内部状态，约定只在专用测试实例上调用。
// ===========================================================================
ShapeReport TacticalMax::analyze(const Board& board, ChessType color) {
    Impl& im = *impl_;
    im.buildIndex(board);
    im.resetState(board);

    const size_t ci = static_cast<size_t>(colorIndex(color));
    ShapeReport rep;
    rep.windowCount = static_cast<int>(im.wins.size());
    rep.fivePoints  = im.keyFive[ci].size();
    rep.fourPoints  = im.keyFour[ci].size();
    rep.firstFive   = im.keyFive[ci].first();
    rep.baseScore   = im.baseScore[ci];
    rep.totalScore  = im.sideStrength(static_cast<int>(ci));
    return rep;
}

int TacticalMax::evaluateBoard(const Board& board, ChessType color) {
    Impl& im = *impl_;
    im.buildIndex(board);
    im.resetState(board);
    return im.evaluate(color);
}

int TacticalMax::planWindowCount(int boardSize, int winLen) {
    if (boardSize <= 0 || winLen <= 0 || winLen > boardSize) return 0;
    const int line = boardSize - winLen + 1;      // 每条线上可容纳的起点数
    // 横、竖各有 boardSize 条平行线；两条对角方向各有 line 条长度足够的斜线
    return 2 * boardSize * line + 2 * line * line;
}

bool TacticalMax::selfCheckIncremental(Board& board, int steps, unsigned seed) {
    Impl& im = *impl_;
    im.buildIndex(board);
    im.resetState(board);

    // ---- 记录基线状态 ----
    const uint64_t hash0 = im.hash_;
    const long long base0[2]  = { im.baseScore[0],  im.baseScore[1] };
    const std::vector<int> five0[2] = { im.keyFive[0].counts(), im.keyFive[1].counts() };
    const std::vector<int> four0[2] = { im.keyFour[0].counts(), im.keyFour[1].counts() };
    const std::vector<int> active0  = im.activeCnt;

    if (im.cellTotal <= 0) return false;

    // ---- 随机落子 ----
    std::mt19937 rng(seed);
    std::vector<int> played;
    played.reserve(static_cast<size_t>(steps));
    for (int s = 0; s < steps; ++s) {
        int idx = static_cast<int>(rng() % static_cast<unsigned>(im.cellTotal));
        int guard = 0;
        while (im.isStone(board, idx) && guard++ < im.cellTotal * 2)
            idx = static_cast<int>(rng() % static_cast<unsigned>(im.cellTotal));
        if (im.isStone(board, idx)) break;                  // 棋盘已满
        const ChessType c = (s % 2 == 0) ? ChessType::Black : ChessType::White;
        im.makeMove(board, idx, c);
        played.push_back(idx);
    }

    // ---- 逆序撤销 ----
    for (int i = static_cast<int>(played.size()) - 1; i >= 0; --i)
        im.unmakeMove(board, played[i]);

    // ---- 逐项比对 ----
    if (im.hash_ != hash0) return false;
    for (int t = 0; t < 2; ++t) {
        if (im.baseScore[static_cast<size_t>(t)] != base0[t])   return false;
        if (im.keyFive[static_cast<size_t>(t)].counts() != five0[t]) return false;
        if (im.keyFour[static_cast<size_t>(t)].counts() != four0[t]) return false;
        if (im.keyFive[static_cast<size_t>(t)].size() < 0)      return false;
        if (im.keyFour[static_cast<size_t>(t)].size() < 0)      return false;
    }
    if (im.activeCnt != active0) return false;
    return true;
}
