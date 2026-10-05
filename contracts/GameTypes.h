// ============================================================
// contracts/GameTypes.h - 跨层共享的纯数据类型（无逻辑、无依赖）
//
// 【微内核分层】这是最底层：契约层。内核、插件、宿主都依赖它，
// 它不依赖任何一方。任何跨边界传递的数据都必须在这里定义，
// 从而保证「谁也不能偷偷依赖谁」。
//
// 本文件刻意只包含 <cstdint> / <string> / <vector> 与 JSON 声明，
// 不含任何 .cpp 逻辑，可被玩家插件与界面插件同时包含。
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gomoku {

// ---- 棋子颜色 ----
// 用 int 序列化（Black=1 / White=-1 / Empty=0），与旧存档格式保持一致。
enum class Stone : int {
    Empty = 0,
    Black = 1,
    White = -1,
};

inline Stone opponent(Stone s) {
    return static_cast<Stone>(-static_cast<int>(s));
}

inline const char* stoneName(Stone s) {
    switch (s) {
        case Stone::Black: return "Black";
        case Stone::White: return "White";
        default:           return "None";
    }
}

// ---- 棋盘坐标 ----
struct Coord {
    int r = -1;
    int c = -1;
    bool valid() const { return r >= 0 && c >= 0; }
};

constexpr Coord kNoCoord{ -1, -1 };

// ---- 对局状态（观察者可读的稳定字符串，便于直接进 JSON / 前端） ----
namespace status {
inline constexpr const char* kIdle       = "Idle";        // 未开局
inline constexpr const char* kInProgress = "InProgress";  // 对局中
inline constexpr const char* kBlackWin   = "BlackWin";    // 黑胜
inline constexpr const char* kWhiteWin   = "WhiteWin";    // 白胜
inline constexpr const char* kDraw       = "Draw";        // 和棋
}  // namespace status

inline bool isFinished(const std::string& s) {
    return s == status::kBlackWin || s == status::kWhiteWin || s == status::kDraw;
}

// ---- 一局棋的规则配置（由界面插件在「开新局」时提供） ----
struct RulesConfig {
    int boardSize = 15;   // N×N（4..30）
    int winLength = 5;    // 连珠获胜数（4..boardSize）

    // 归一化到可玩范围。两条硬约束：
    //   1) 连珠数至少 4（少于 4 不是五子棋，也几乎必然立刻结束）
    //   2) 连珠数不能超过棋盘边长（否则这盘棋永远赢不了，只能下到和棋）
    // 处理顺序刻意是"先修连珠数、再修尺寸、最后夹住连珠数"，
    // 这样无论用户/存档给出多离谱的组合，结果一定是一盘能正常结束的棋。
    RulesConfig normalized() const {
        RulesConfig out = *this;
        if (out.winLength < 4) out.winLength = 5;      // 未设置或明显非法 → 回到默认

        if (out.boardSize < 4 || out.boardSize > 30) out.boardSize = 15;
        if (out.boardSize < out.winLength) out.boardSize = out.winLength;

        if (out.winLength > out.boardSize) out.winLength = out.boardSize;
        return out;
    }
};

// ---- 一局棋的玩家座位 ----
struct Seat {
    std::string playerId;    // 玩家插件 id（如 "human" / "tactical-max"）
    std::string playerName;  // 显示名快照（记录/回放用）
};

// ---- 一条落子记录（内核唯一事实来源就是它） ----
struct Move {
    int r = -1;
    int c = -1;
    Stone color = Stone::Empty;
    int64_t timestampMs = 0;
};

// ============================================================
// ViewState - 内核状态的「只读快照」，是界面插件唯一能看到的东西。
//
// 为什么要有它：界面插件不应该拿到可变的棋盘对象，否则任意界面都能
// 改棋局，内核边界就没了。界面只看快照 + 通过 IControllable 发命令。
// ============================================================
struct ViewState {
    // ---- 对局标识 ----
    std::string status = status::kIdle;   // Idle/InProgress/BlackWin/WhiteWin/Draw
    int boardSize = 15;
    int winLength = 5;

    // 本进程内每开一局就 +1 的对局序号（从 1 开始，0 表示还没开过局）。
    // 界面靠它区分"新的一局"与"同一局继续"——只看步数会被
    // "上一局结束、下一局又走到相同步数"骗过去（自检里真实踩过）。
    long long matchId = 0;

    // ---- 棋盘（一维展开：r*boardSize+c，值取 Stone 的 int） ----
    std::vector<int> cells;

    // ---- 回合与步数 ----
    Stone turn = Stone::Black;
    int moveCount = 0;
    Coord lastMove{ -1, -1 };        // 全局最后一手（闪烁标记）
    Coord lastBlack{ -1, -1 };
    Coord lastWhite{ -1, -1 };
    std::vector<Move> history;       // 可供回放/悔棋重建

    // ---- 双方信息 ----
    std::string blackId, blackName;
    std::string whiteId, whiteName;

    // ---- 运行时标志 ----
    bool thinking = false;           // 当前回合方正在思考（AI 线程中）
    bool humanTurn = false;          // 当前回合方是否为人类（界面据此接受点击）
    bool canUndo = false;            // 是否可悔棋
    bool storageEnabled = true;      // 记忆存储开关
    long long version = 0;           // 状态版本号（观察者据此判断是否变化）

    // ---- 界面提示（错误/成功消息，一次性消费） ----
    std::string message;

    // ---- 便捷读取 ----
    Stone at(int r, int c) const {
        if (r < 0 || c < 0 || r >= boardSize || c >= boardSize) return Stone::Empty;
        return static_cast<Stone>(cells[static_cast<size_t>(r) * boardSize + c]);
    }
    bool finished() const { return isFinished(status); }
};

// ---- 玩家插件的元信息（供界面下拉框 / 目录展示，不含任何算法） ----
struct PlayerInfo {
    std::string id;             // 稳定标识（存档/接口用），如 "tactical-max"
    std::string display;        // 界面显示名，如 "TacticalMax"
    std::string description;    // 一句话说明，界面可作提示
    bool human = false;         // 是否人类（落子来自界面输入）
    bool ready = true;          // 是否可用（如 API 玩家未配置时为 false）
    std::string fallbackId;     // 不可用时的回退玩家 id（空表示不回退）
};

// ---- 界面插件（视图）的元信息 ----
struct ViewInfo {
    std::string id;             // 如 "cli" / "web"
    std::string display;
    std::string description;
};

// ---- 存储目录项的元信息（棋局/残局列表用） ----
// 放在契约层而不是 kernel，是因为界面要通过 IControllable 拿列表：
// 契约层是最底层，绝不能反过来 include kernel —— 这条规则一旦破例，
// 「换内核不影响界面」就不成立了。
struct StoredBrief {
    std::string id;
    int boardSize = 15;
    int winLength = 5;
    std::string black, white;
    std::string blackId, whiteId;
    int moves = 0;
    std::string note;
};

}  // namespace gomoku
