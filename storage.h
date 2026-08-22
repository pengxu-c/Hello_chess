// ============================================================
// storage.h - 棋局存储管理类声明
// 功能：悔棋扩展、棋局回访、残局保存/载入、全局步数统计、控制台命令
// 设计原则：高可扩展性
//   - 接口分离：StorageManager 提供清晰公共接口
//   - 配置驱动：StorageConfig 控制所有行为
//   - 格式可扩展：key=value 文本格式，未知字段忽略，新增字段不破坏旧版
//   - 命令模式：控制台命令可扩展（handleConsoleCommand）
//   - 状态枚举：GameStatus 易于扩展新状态
//   - 前置声明：减少耦合
// 数据目录结构：
//   data/
//     stats.txt              全局统计
//     games/                 完整棋局记录
//       G20260818_153000_001.txt
//     resumes/               残局记录（可恢复继续）
//       R20260818_153000_001.txt
// ============================================================
#pragma once
#include "core.h"
#include <vector>
#include <string>
#include <cstdint>

class Board;

// ---- 单步落子记录 ----
struct MoveRecord {
    int step = 0;                            // 步骤序号（从 0 开始）
    int r = -1;                              // 行
    int c = -1;                              // 列
    ChessType color = ChessType::None;      // 颜色
    int64_t timestamp = 0;                   // 时间戳（毫秒）
};

// ---- 棋局状态（可扩展） ----
enum class GameStatus {
    InProgress = 0,   // 进行中
    BlackWin     = 1, // 黑胜
    WhiteWin     = 2, // 白胜
    Draw         = 3, // 平局
    Aborted      = 4, // 中止（残局）
    Resumed      = 5  // 从残局恢复
};

// ---- 棋局记录 ----
struct GameRecord {
    std::string id;                                // 唯一编号
    int boardSize = 15;                            // 棋盘尺寸
    int winLength = 5;                             // 连珠数
    std::string player1Name;                       // 玩家1名称（执黑）
    std::string player2Name;                       // 玩家2名称（执白）
    int player1Type = 1;                           // 玩家1类型编号
    int player2Type = 1;                           // 玩家2类型编号
    std::vector<MoveRecord> moves;                 // 所有步骤
    GameStatus status = GameStatus::InProgress;   // 状态
    int64_t startTime = 0;                         // 开始时间
    int64_t endTime = 0;                           // 结束时间
    int blackMoves = 0;                            // 黑方步数
    int whiteMoves = 0;                            // 白方步数
    std::string note;                              // 备注
};

// ---- 全局统计 ----
struct GlobalStats {
    int totalGames = 0;          // 总局数
    int blackWins = 0;           // 黑胜次数
    int whiteWins = 0;           // 白胜次数
    int draws = 0;               // 平局次数
    int aborts = 0;              // 中止次数
    int blackTotalMoves = 0;     // 黑方累计步数
    int whiteTotalMoves = 0;     // 白方累计步数
    int64_t lastUpdate = 0;      // 最后更新时间
};

// ---- 存储配置 ----
struct StorageConfig {
    bool enabled = false;                    // 是否启用记忆存储（默认关闭）
    std::string dataDir    = "data";         // 数据根目录
    std::string gamesDir   = "games";        // 棋局子目录
    std::string resumesDir = "resumes";      // 残棋残局子目录
    std::string statsFile  = "stats.txt";    // 统计文件名
    std::string fileExt    = ".txt";         // 文件扩展名
};

// ---- 存储管理类 ----
class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    // ====== 配置 ======
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void setConfig(const StorageConfig& cfg);
    const StorageConfig& config() const;

    // ====== 棋局生命周期 ======
    void startGame(int boardSize, int winLength,
                   const std::string& p1, const std::string& p2,
                   int p1Type = 1, int p2Type = 1);
    void recordMove(int r, int c, ChessType color);
    void endGame(GameStatus status);
    bool isInGame() const;
    const std::string& currentGameId() const;
    const GameRecord& currentRecord() const;

    // ====== 悔棋 ======
    bool canUndo() const;                     // 是否可悔棋
    bool undoLastMove(Board& board);          // 悔棋最后一步
    int  undoMoves(Board& board, int n);      // 悔棋 n 步，返回实际悔棋数
    int  moveCount() const;                   // 当前步数

    // ====== 棋局回访 ======
    std::vector<std::string> listGames() const;          // 列出所有棋局 ID
    std::vector<GameRecord> listGameRecords() const;     // 列出所有棋局记录
    bool loadGame(const std::string& id, GameRecord& record) const;
    bool replayGame(const std::string& id, Board& board, int upToStep = -1) const;

    // ====== 残局保存/载入 ======
    std::vector<std::string> listResumes() const;
    bool saveResume(const std::string& note = "");
    bool loadResume(const std::string& id, GameRecord& record) const;
    bool restoreResume(const std::string& id, Board& board,
                       int& boardSize, int& winLength,
                       int& p1Type, int& p2Type);

    // ====== 全局统计 ======
    GlobalStats globalStats() const;
    void printGlobalStats() const;

    // ====== 控制台命令 ======
    bool handleConsoleCommand(const std::string& cmd, Board& board);
    void printHelp() const;
    bool isAbortRequested() const;           // 是否请求中止
    void clearAbortRequest();

    // ====== 非阻塞控制台输入 ======
    static std::string tryReadConsoleLine();

private:
    StorageConfig config_;
    GameRecord current_;
    GlobalStats globalStats_;
    bool inGame_ = false;
    bool abortRequested_ = false;

    // 工具
    std::string generateId(const std::string& prefix) const;
    std::string dirPath(const std::string& subdir) const;
    std::string filePath(const std::string& subdir, const std::string& filename) const;
    void ensureDir(const std::string& dir) const;
    std::vector<std::string> listIds(const std::string& subdir) const;  // 列出某子目录全部记录ID(去扩展名,排序)
    bool saveRecord(const GameRecord& record, const std::string& subdir) const;
    bool loadRecord(const std::string& filepath, GameRecord& record) const;
    int64_t nowMs() const;
    void loadGlobalStats();
    void saveGlobalStats() const;
    void updateGlobalStats(const GameRecord& record);

    // 序列化辅助
    std::string statusToString(GameStatus s) const;
    GameStatus stringToStatus(const std::string& s) const;
};