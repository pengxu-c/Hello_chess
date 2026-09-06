// ============================================================
// threat.cpp - 必胜/必防威胁检测类实现
// 统一实现成连位计数与各威胁层级判定，供 GreedyScoringAI /
// MinimaxPP 复用。所有"模拟落子 → 判定 → 恢复"均限定在
// nearbyEmpties 邻域候选内，避免全盘 O(N²) 扫描。
// ============================================================
#include "threat.h"

ThreatDetector::ThreatDetector(Board& board) : board_(board) {}

// 候选空位生成：收集已有棋子周围 radius 内的空位（去重）。
// 正确性论证：任何"成连位"必然紧邻已有棋子——连珠需连续，落空位成
// winLen 连意味着该空位与 winLen-1 个同色棋子相邻；活三/活四威胁位
// 同理紧邻已有棋子。故 radius=2 保守覆盖所有真实威胁，不改变检测结果。
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

// 成连位计数：模拟落 (r,c) 为 color，数"再落一子即成 winLen 连"的空位数。
// 关键洞察：落 (r,c) 后新增的成连位必然在过 (r,c) 的线段上——不含 (r,c) 的
// 线段不被落子改变。故沿 4 方向 scanLine 即可 O(winLen) 数完，无需再遍历候选。
// 计数规则：某方向连续同色 count == winLen-1 且一端开放时，该开放端即一个成连位；
// count < winLen-1 落开放端仍不足；count >= winLen 已是"直接成连"（1 步必胜范畴）。
int ThreatDetector::countWinSpots(int r, int c, ChessType color) const {
    int winLen = board_.winLen();
    board_.set(r, c, color);
    int spots = 0;
    int dr[] = { 0, 1, 1, 1 }, dc[] = { 1, 0, 1, -1 };
    for (int d = 0; d < 4; d++) {
        LineInfo li = scanLine(board_, r, c, dr[d], dc[d], color);
        if (li.count == winLen - 1) {   // 差一子成连，两端开放处即成连位
            if (li.openStart) spots++;
            if (li.openEnd)   spots++;
        }
    }
    board_.set(r, c, ChessType::None);
    return spots;
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

// 2 步必胜：color 落子后成连位 >= 2（成活四等双威胁），且落子后对方无
// 1 步成连反制（否则对方先赢，必须先去防守）。返回首个命中位。
Pos ThreatDetector::twoStepWin(ChessType color, ChessType opp) const {
    auto cands = nearbyEmpties(board_);
    for (const auto& p : cands) {
        if (board_.at(p.r, p.c) != ChessType::None) continue;
        if (countWinSpots(p.r, p.c, color) < 2) continue;
        // 落 p 后检查对方是否可 1 步成连反制
        board_.set(p.r, p.c, color);
        bool oppCounter = oneStepWin(opp).valid();
        board_.set(p.r, p.c, ChessType::None);
        if (!oppCounter) return p;
    }
    return { -1, -1 };
}

// 对方 2 步必胜第一步位：对方落在这些位上会形成 >= 2 个成连位（如活三
// 的两端、双冲四）。防守方抢占这些位，阻止对方形成活四等不可防局面。
// 注意：对方"直接成连"的位不在此列（那属于对方 1 步必胜，由 oneStepWin 兜底）。
std::vector<Pos> ThreatDetector::mustBlockTwo(ChessType opp) const {
    std::vector<Pos> res;
    auto cands = nearbyEmpties(board_);
    for (const auto& p : cands) {
        if (board_.at(p.r, p.c) != ChessType::None) continue;
        if (countWinSpots(p.r, p.c, opp) >= 2) res.push_back(p);
    }
    return res;
}

// 对方 3 步必防：对方落在这些位上会同时形成 >= 2 个活三（双活三创建位）。
// 双活三是 3 步必胜（对方 1 步造双活三 → 防守方堵其一个活三 → 对方从另一活三
// 2 步获胜），超出 2 步必胜框架，需单独检测并提前抢占。
// 判定：落子后沿 4 方向统计"活三"（count == winLen-2 且两端开放）数量 >= 2。
std::vector<Pos> ThreatDetector::mustBlockDoubleThree(ChessType opp) const {
    std::vector<Pos> res;
    int winLen = board_.winLen();
    int dr[] = { 0, 1, 1, 1 }, dc[] = { 1, 0, 1, -1 };
    auto cands = nearbyEmpties(board_);
    for (const auto& p : cands) {
        if (board_.at(p.r, p.c) != ChessType::None) continue;
        board_.set(p.r, p.c, opp);
        int threes = 0;
        for (int d = 0; d < 4; d++) {
            LineInfo li = scanLine(board_, p.r, p.c, dr[d], dc[d], opp);
            if (li.count == winLen - 2 && li.openStart && li.openEnd) threes++;
        }
        board_.set(p.r, p.c, ChessType::None);
        if (threes >= 2) res.push_back(p);
    }
    return res;
}