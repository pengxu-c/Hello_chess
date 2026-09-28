// ============================================================================
// bench_main.cpp - Gomoku 引擎基准测试命令行入口
//
// 用途：给开发者与 agent 做「棋力 / 正确性回归」的命令行工具。
// 设计原则：
//   * 纯文本、零交互、无 UI、无网络 —— 可随时在 CI 或本地命令行重跑；
//   * 每条输出同时打到控制台与日志（out/ 目录），便于长期留存与复盘；
//   * 日志含完整棋谱坐标，失败时可直接还原整盘棋。
//
// 用法：
//   bench --list
//   bench --selftest
//   bench --p1 7 --p2 5 --games 10 --budget 1500
//   bench --p1 7 --p2 4 --size 20 --winlen 6 --games 6
//   bench --p1 5 --p2 7 --random-open
//   bench --p1 7 --p2 5 --out <path>
//
// 日志目录默认写在相对路径 <cwd>/clattervault/ 下
// （"clatter" = 棋子落盘的清脆声响，vault = 归档地）。
// 文件名形如 chronicle-20260928-233000-p7v5.log，含完整棋谱。
// ============================================================================

#include "bench_match.h"
#include "bench_selftest.h"

#include "../core.h"
#include "../player.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#if defined(_WIN32)
    #include <direct.h>
    #define MK_DIR(path) ::_mkdir(path)
#else
    #include <sys/stat.h>
    #define MK_DIR(path) ::mkdir(path, 0755)
#endif

namespace {

// ---------------------------------------------------------------------------
// 同时输出到控制台与日志文件的记录器
// ---------------------------------------------------------------------------
class Chronicle {
public:
    explicit Chronicle(const std::string& path) : path_(path) {
        f_ = std::fopen(path.c_str(), "w");
    }
    ~Chronicle() { if (f_) std::fclose(f_); }

    bool ok() const { return f_ != nullptr; }
    const std::string& path() const { return path_; }

    void line(const char* fmt, ...) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        std::printf("%s\n", buf);
        if (f_) { std::fprintf(f_, "%s\n", buf); std::fflush(f_); }
    }

private:
    std::string path_;
    FILE* f_ = nullptr;
};

std::string nowStamp(const char* fmt) {
    char buf[64] = { 0 };
    const std::time_t now = std::time(nullptr);
    const std::tm* t = std::localtime(&now);
    if (!t) return std::string("unknown");
    std::strftime(buf, sizeof(buf), fmt, t);
    return std::string(buf);
}

// 列号 → 字母标号：0=A ... 25=Z, 26=AA ...（棋盘边长上限 30）
std::string colLabel(int c) {
    std::string s;
    c += 1;                                  // 转成 1 基
    while (c > 0) {
        const int rem = (c - 1) % 26;
        s.insert(s.begin(), static_cast<char>('A' + rem));
        c = (c - 1) / 26;
    }
    return s;
}

std::string moveLabel(const Pos& p) {
    return colLabel(p.c) + std::to_string(p.r + 1);
}

std::string joinMoves(const std::vector<Pos>& moves) {
    std::string s;
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i > 0) s += " ";
        s += moveLabel(moves[i]);
        if (s.size() > 4000) { s += " ..."; break; }   // 超长棋谱截断，防止日志膨胀
    }
    return s;
}

void printUsage() {
    std::printf(
        "Usage:\n"
        "  bench --list\n"
        "  bench --selftest\n"
        "  bench --p1 <id> --p2 <id> [options]\n"
        "\n"
        "Options:\n"
        "  --p1 <id>        first player (black), default 7\n"
        "  --p2 <id>        second player (white), default 5\n"
        "  --games <n>      number of games, default 10\n"
        "  --budget <ms>    per-move time budget, default 1500\n"
        "  --size <n>       board size, default 15\n"
        "  --winlen <k>     win length, default 5\n"
        "  --random-open    random opening instead of fixed center\n"
        "  --seed <n>       random seed used by --random-open\n"
        "  --out <path>     log directory (relative), default 'clattervault'\n"
        "\n");
}

void printPlayerList() {
    std::printf("Playable IDs for bench:\n");
    for (int id : benchPlayerIds())
        std::printf("  %d  %s\n", id, playerIdName(id));
    std::printf("\nNote: 1=Human and 6=API AI are excluded (need interaction / network).\n");
}

}  // namespace

int main(int argc, char** argv) {
    bool selftest = false, listOnly = false, randomOpen = false;
    std::string outDir = "clattervault";
    MatchConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto nextInt = [&]() -> int {
            return (i + 1 < argc) ? std::atoi(argv[++i]) : 0;
        };
        if (a == "--selftest")          selftest = true;
        else if (a == "--list")         listOnly = true;
        else if (a == "--random-open")  randomOpen = true;
        else if (a == "--p1")           cfg.p1 = nextInt();
        else if (a == "--p2")           cfg.p2 = nextInt();
        else if (a == "--games")        cfg.games = nextInt();
        else if (a == "--budget")       cfg.budgetMs = nextInt();
        else if (a == "--size")         cfg.boardSize = nextInt();
        else if (a == "--winlen")       cfg.winLen = nextInt();
        else if (a == "--seed")         cfg.seed = static_cast<unsigned>(nextInt());
        else if (a == "--out")          outDir = (i + 1 < argc) ? argv[++i] : "out";
        else if (a == "--help" || a == "-h") { printUsage(); return 0; }
        else {
            std::printf("Unknown option: %s\n\n", a.c_str());
            printUsage();
            return 2;
        }
    }

    cfg.fixedOpening = !randomOpen;

    if (listOnly) { printPlayerList(); return 0; }

    // ---- 输出目录 ----
    MK_DIR(outDir.c_str());
    const std::string logName = outDir + "/chronicle-" + nowStamp("%Y%m%d-%H%M%S")
                              + "-p" + std::to_string(cfg.p1) + "v" + std::to_string(cfg.p2)
                              + ".log";
    Chronicle log(logName);
    if (!log.ok())
        std::printf("[warn] cannot create log file: %s (console only)\n", logName.c_str());

    log.line("================ Gomoku Chronicle ================");
    log.line("started  : %s", nowStamp("%Y-%m-%d %H:%M:%S").c_str());

    // ================= 自测模式 =================
    if (selftest) {
        log.line("mode     : selftest");
        log.line("------------------------------------------------");

        std::vector<TestResult> results;
        const int failed = runSelfTests(true, &results);

        for (const TestResult& r : results) {
            log.line("[%-4s] %s%s%s",
                     r.passed ? "PASS" : "FAIL", r.name.c_str(),
                     r.passed ? "" : "  -> ", r.passed ? "" : r.detail.c_str());
        }
        log.line("------------------------------------------------");
        log.line("result   : %zu/%zu passed", results.size() - static_cast<size_t>(failed),
                 results.size());
        log.line("====================================================");
        return failed == 0 ? 0 : 1;
    }

    // ================= 对弈模式 =================
    if (!isPlayableId(cfg.p1) || !isPlayableId(cfg.p2)) {
        std::printf("Invalid player id. Use --list to see available IDs.\n");
        return 2;
    }
    if (cfg.winLen > cfg.boardSize) {
        std::printf("Invalid rule: winlen %d > size %d\n", cfg.winLen, cfg.boardSize);
        return 2;
    }

    Judge judge;
    log.line("mode     : match");
    log.line("players  : BLACK(#%d %s)  vs  WHITE(#%d %s)",
             cfg.p1, playerIdName(cfg.p1), cfg.p2, playerIdName(cfg.p2));
    log.line("board    : %d x %d, winlen %d", cfg.boardSize, cfg.boardSize, cfg.winLen);
    log.line("budget   : %d ms/move, games %d, opening %s",
             cfg.budgetMs, cfg.games, cfg.fixedOpening ? "fixed-center" : "random");
    log.line("------------------------------------------------");

    MatchSummary sum;
    for (int g = 0; g < cfg.games; ++g) {
        auto p1 = createPlayerById(cfg.p1, judge, nullptr);
        auto p2 = createPlayerById(cfg.p2, judge, nullptr);

        const GameRecord rec = playOneGame(cfg, *p1, *p2);
        ++sum.games;

        if (rec.aborted) {
            ++sum.aborted;
            log.line("game %2d  | ABORTED (%s)", g + 1, rec.note.c_str());
            continue;
        }

        if (rec.winner == ChessType::Black)      ++sum.p1Wins;
        else if (rec.winner == ChessType::White) ++sum.p2Wins;
        else                                     ++sum.draws;

        const int bMoves = (rec.moveCount + 1) / 2;
        const int wMoves = rec.moveCount / 2;
        const double bAvg = bMoves > 0 ? rec.blackTotalMs / bMoves : 0.0;
        const double wAvg = wMoves > 0 ? rec.whiteTotalMs / wMoves : 0.0;
        sum.avgMoves += rec.moveCount;
        sum.p1AvgMs  += bAvg;
        sum.p2AvgMs  += wAvg;
        sum.p1MaxMs   = std::max(sum.p1MaxMs, rec.blackMaxMs);
        sum.p2MaxMs   = std::max(sum.p2MaxMs, rec.whiteMaxMs);

        log.line("game %2d  | winner %-6s | %3d moves | black avg %7.1fms max %7.1fms | white avg %7.1fms max %7.1fms",
                 g + 1,
                 (rec.winner == ChessType::Black) ? "BLACK" :
                 (rec.winner == ChessType::White) ? "WHITE" : "DRAW ",
                 rec.moveCount, bAvg, rec.blackMaxMs, wAvg, rec.whiteMaxMs);
        log.line("          | moves: %s", joinMoves(rec.moves).c_str());
    }

    const int decided = sum.p1Wins + sum.p2Wins + sum.draws;
    if (decided > 0) {
        sum.avgMoves /= decided;
        sum.p1AvgMs  /= decided;
        sum.p2AvgMs  /= decided;
        sum.winRate  = (sum.p1Wins + 0.5 * sum.draws) / decided;
    }

    log.line("------------------------------------------------");
    log.line("summary  : BLACK(#%d %s) %d W / %d L / %d D   rate %.1f%%",
             cfg.p1, playerIdName(cfg.p1), sum.p1Wins, sum.p2Wins, sum.draws, sum.winRate * 100.0);
    log.line("           avg moves %.1f | black avg %7.1fms max %7.1fms | white avg %7.1fms max %7.1fms",
             sum.avgMoves, sum.p1AvgMs, sum.p1MaxMs, sum.p2AvgMs, sum.p2MaxMs);
    if (sum.aborted > 0) log.line("           aborted games: %d", sum.aborted);
    log.line("====================================================");

    return 0;
}
