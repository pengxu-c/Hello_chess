// ============================================================
// threat.cpp - 必胜/必防威胁检测类实现
// 统一实现"威胁计数"与必胜/必防判定，供 GreedyScoringAI /
// MinimaxPP / EasyJudgeAI 复用。所有"模拟落子 → 判定 → 恢复"
// 均限定在 nearbyEmpties 邻域候选内，避免全盘 O(N²) 扫描。
// ============================================================
#include "threat.h"

ThreatDetector::ThreatDetector(Board& board) : board_(board) {}

// 候选空位生成：收集已有棋子周围 radius 内的空位（去重）。
// 正确性论证：任何威胁位必然紧邻已有棋子——成连位是 n-1 个同色棋子旁的空位，
// 真活三的折算同理紧邻已有棋子。故 radius=2 保守覆盖所有真实威胁。
std::vector<Pos> ThreatDetector::nearbyEmpties(const Board& board, int radius) {
    std::vector<Pos> res;
    int n = board.size();
    std::vector<bool> seen(static_cast<size_t>(n) * n, false);   // 去重标记
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            if (board.at(r, c) == ChessType::None) continue;
            for (int dr = -radius; dr <= radius; dr++) {
                for (int dc = -radius; dc <= radius; dc++) {
                    int nr = r + dr, nc = c + dc;
                    if (board.inBounds(nr, nc) && board.at(nr, nc) == ChessType::None) {
                        size_t idx = static_cast<size_t>(nr) * n + nc;
                        if (!seen[idx]) { seen[idx] = true; res.push_back({ nr, nc }); }
                    }
                }
            }
        }
    }
    return res;
}

// 威胁计数：模拟落 (r,c) 为 color，沿 4 方向各扫一次，把「成连位」与「折算真活三」
// 累加成一个威胁数。>= 2 即强制胜（两道威胁对方单点堵不完）。
//
// 归责规则（每个方向 count 唯一，只进一个分支，杜绝重复计数）：
//   1) count >= winLen       → +2（已成连，绝杀）
//   2) count == winLen-1     → 开放端各 +1（即成连位；活四两端开放记 2，冲四记 1）
//   3) count == winLen-2 且两端开放 → 真活三，折算 +1
//   4) 其余                   → +0
//
// 真活三判定：两端开放，且「至少一端再往外一格为空」——落该端才能延伸成
// 两端开放的 n-1 连（活四）。若两端都只能成眠四（假活三），变不成活四，
// 构不成"下一步强变必胜"的威胁，故不折算。
int ThreatDetector::threatCount(int r, int c, ChessType color) const {
    int winLen = board_.winLen();
    board_.set(r, c, color);
    int threats = 0;
    int dr[] = { 0, 1, 1, 1 }, dc[] = { 1, 0, 1, -1 };
    for (int d = 0; d < 4; d++) {
        LineInfo li = scanLine(board_, r, c, dr[d], dc[d], color);
        if (li.count >= winLen) {
            threats += 2;                          // 已成连：最强威胁
        } else if (li.count == winLen - 1) {
            if (li.openStart) threats++;           // 冲四/活四的成连位
            if (li.openEnd)   threats++;
        } else if (li.count == winLen - 2 && li.openStart && li.openEnd) {
            // 真活三：至少一端再往外一格为空，落该端能延伸成活四
            int r0 = li.pr - dr[d], c0 = li.pc - dc[d];   // 反向外一格
            int r1 = li.nr + dr[d], c1 = li.nc + dc[d];   // 正向外一格
            bool canLive4 = (board_.inBounds(r0, c0) && board_.at(r0, c0) == ChessType::None)
                         || (board_.inBounds(r1, c1) && board_.at(r1, c1) == ChessType::None);
            if (canLive4) threats++;               // 真活三折算 1 成连位
        }
    }
    board_.set(r, c, ChessType::None);
    return threats;
}

// 1 步必胜：color 落在某空位即形成 winLen 连。返回首个命中位。
Pos ThreatDetector::oneStepWin(ChessType color) const {
    int winLen = board_.winLen();
    int dr[] = { 0, 1, 1, 1 }, dc[] = { 1, 0, 1, -1 };
    auto cands = nearbyEmpties(board_);
    for (const auto& p : cands) {
        if (board_.at(p.r, p.c) != ChessType::None) continue;
        board_.set(p.r, p.c, color);
        bool win = false;
        for (int d = 0; d < 4; d++) {
            if (scanLine(board_, p.r, p.c, dr[d], dc[d], color).count >= winLen) { win = true; break; }
        }
        board_.set(p.r, p.c, ChessType::None);
        if (win) return p;
    }
    return { -1, -1 };
}

// 2 步必胜：color 落子后威胁数 >= 2（活四/双冲四/四三/双活三），且落子后对方无
// 1 步成连反制（否则对方先赢，必须先防守）。返回首个命中位。
Pos ThreatDetector::twoStepWin(ChessType color, ChessType opp) const {
    auto cands = nearbyEmpties(board_);
    for (const auto& p : cands) {
        if (board_.at(p.r, p.c) != ChessType::None) continue;
        if (threatCount(p.r, p.c, color) < 2) continue;
        // 落 p 后检查对方是否可 1 步成连反制
        board_.set(p.r, p.c, color);
        bool oppCounter = oneStepWin(opp).valid();
        board_.set(p.r, p.c, ChessType::None);
        if (!oppCounter) return p;
    }
    return { -1, -1 };
}

// 必防：对方落在这些位上会形成 forced win（威胁数 >= 2），防守方抢占第一步位
// 以阻止。覆盖活四/双冲四/四三/双活三。对方"直接成连"的位不在此列（属 1 步必胜，
// 由 oneStepWin 兜底）；单冲四/单活三威胁数为 1，不在此列（可堵）。
std::vector<Pos> ThreatDetector::mustDefend(ChessType opp) const {
    std::vector<Pos> res;
    auto cands = nearbyEmpties(board_);
    for (const auto& p : cands) {
        if (board_.at(p.r, p.c) != ChessType::None) continue;
        if (threatCount(p.r, p.c, opp) >= 2) res.push_back(p);
    }
    return res;
}