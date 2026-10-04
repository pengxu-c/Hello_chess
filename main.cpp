// ============================================================
// main.cpp - 程序入口
// 默认进入 Web 模式：启动本地 HTTP 服务，并用系统浏览器打开界面。
// 传 --cli 参数则走原终端流程，便于脚本 / 调试 / 回归对照。
// 传 --selfplay 参数则走 headless 自对弈（Phase 6 最小评测框架）。
// ============================================================
#include "controller.h"
#include "server/game_server.h"
#include "core.h"
#include "player.h"
#include "ai_config.h"
#include "ai_player.h"
#include "tactical_max.h"
#include "player_registry.h"
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <windows.h>

// 取可执行文件所在目录（返回 UTF-8 窄字符串）
static std::string exeDir() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring ws(buf, n);
    size_t pos = ws.find_last_of(L"\\/");
    std::wstring dir = (pos == std::wstring::npos) ? L"." : ws.substr(0, pos);

    int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? static_cast<size_t>(len - 1) : 0, '\0');
    if (len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, out.data(), len, nullptr, nullptr);
    }
    return out;
}

// 定位前端资源根目录：依次尝试当前目录、exe 同级、exe 上两级
// （build/Release/Hello_chess.exe → chess/web）
static std::string resolveWebRoot() {
    std::string exe = exeDir();
    std::vector<std::string> candidates = {
        "web",
        exe + "/web",
        exe + "/../../web",
        exe + "/../web",
    };
    for (const auto& c : candidates) {
        std::ifstream ifs(c + "/index.html", std::ios::binary);
        if (ifs) return c;
    }
    return exe + "/../../web";   // 兜底：交由启动自检给出明确报错
}

// --cli 模式按需附加控制台：
// 程序编译为 Windows 子系统（双击运行无控制台窗口），
// 从 cmd 启动时附加到父控制台，双击 --cli 时新建一个控制台。
static void attachConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
}

// 原终端流程：选玩家 → 循环对局
static int runCli() {
    attachConsole();
    GameController gc;
    gc.run();
    // 退出前暂停：双击 exe 运行时进程结束会立即关闭控制台窗口，
    // 导致最后的输出来不及查看。等待用户按回车后再退出。
    printf("\nPress Enter to exit...");
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
    return 0;
}

// ============================================================
// headless 自对弈（Phase 6 最小评测框架）
// 绕过 UI 与 HTTP，让两个 AI 直接在内存棋盘上对弈并输出胜率，
// 用于量化 TacticalMax 相对其它档位的棋力变化，避免"凭感觉"评估。
// 注意：无禁手五子棋先手优势极大，比较不同算法时应交换先后手统计。
// ============================================================

// 自对弈日志文件句柄。Windows 子系统下从脚本启动时 stdout 不可靠
// （无父控制台，freopen/printf 可能 access violation），故只写文件。
static FILE* g_selfplayLog = nullptr;

// 写自对弈日志（仅文件，避免依赖控制台）
static void spOut(const char* fmt, ...) {
    if (!g_selfplayLog) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_selfplayLog, fmt, ap);
    va_end(ap);
    fflush(g_selfplayLog);
}

// 按编号创建 AI 棋手（统一走玩家注册表；headless 场景人类/未知编号回退 Minimax++）
static Player* makeAiPlayer(int type, Judge& judge, const AIConfig& cfg) {
    if (!playerRegistry().contains(type) || playerRegistry().isHuman(type)) type = 5;
    PlayerContext ctx{ &judge, nullptr, &cfg };
    return playerRegistry().create(type, ctx).release();
}

// 跑 games 局 p1(黑) vs p2(白)，结果写入 selfplay.log
static int runSelfplay(int games, int p1, int p2, int budgetMs) {
    fopen_s(&g_selfplayLog, "selfplay.log", "w");
    spOut("[selfplay] P%d (Black) vs P%d (White), %d games, tactical budget=%dms\n",
          p1, p2, games, budgetMs);

    AIConfig cfg;
    cfg.loadFromFile();
    Judge judge;
    Board board;

    int win1 = 0, win2 = 0, draw = 0;
    for (int g = 0; g < games; ++g) {
        board.resize(15);
        board.setWinLen(5);
        board.clear();

        Player* a = makeAiPlayer(p1, judge, cfg);
        Player* b = makeAiPlayer(p2, judge, cfg);
        if (auto* tm = dynamic_cast<TacticalMax*>(a)) tm->setTimeBudgetMs(budgetMs);
        if (auto* tm = dynamic_cast<TacticalMax*>(b)) tm->setTimeBudgetMs(budgetMs);

        ChessType cur = ChessType::Black;
        int result = 0;   // 1=黑(p1)胜, -1=白(p2)胜, 0=和
        const int maxSteps = board.size() * board.size();
        for (int step = 0; step < maxSteps; ++step) {
            Player* pl = (cur == ChessType::Black) ? a : b;
            Pos m = pl->place(board, cur);
            if (!m.valid() || !board.inBounds(m.r, m.c) ||
                board.at(m.r, m.c) != ChessType::None) {
                result = (cur == ChessType::Black) ? -1 : 1;   // 无法落子即判负
                break;
            }
            board.place(m.r, m.c, cur);
            if (judge.checkWin(board, m, cur)) {
                result = (cur == ChessType::Black) ? 1 : -1;
                break;
            }
            if (board.isFull()) { result = 0; break; }
            cur = opponent(cur);
        }
        delete a;
        delete b;

        if (result > 0)      { ++win1; spOut("game %2d: P%d (Black) wins\n", g + 1, p1); }
        else if (result < 0) { ++win2; spOut("game %2d: P%d (White) wins\n", g + 1, p2); }
        else                 { ++draw; spOut("game %2d: draw\n", g + 1); }
    }

    spOut("[selfplay] P%d wins %d | P%d wins %d | draws %d\n", p1, win1, p2, win2, draw);
    spOut("[selfplay] P%d score = %.1f%%\n", p1,
          games > 0 ? 100.0 * (win1 + 0.5 * draw) / games : 0.0);
    if (g_selfplayLog) { fclose(g_selfplayLog); g_selfplayLog = nullptr; }
    return 0;
}

int main(int argc, char** argv) {
    // 解析命令行参数：
    //   --cli              终端流程
    //   --port N           指定端口
    //   --no-browser       不自动开浏览器
    //   --selfplay [N]     headless 自对弈 N 局（默认 10）
    //   --p1 N --p2 M      自对弈双方编号（默认 7 vs 5）
    //   --budget ms        TacticalMax 单步时间预算（默认 1500）
    int port = 0;
    bool openBrowser = true;
    bool doSelfplay = false;
    int spGames = 10, spP1 = 7, spP2 = 5, spBudget = 1500;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli") {
            return runCli();
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--no-browser") {
            openBrowser = false;
        } else if (arg == "--selfplay") {
            doSelfplay = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                spGames = std::atoi(argv[++i]);
                if (spGames < 1) spGames = 1;
            }
        } else if (arg == "--p1" && i + 1 < argc) {
            spP1 = std::atoi(argv[++i]);
        } else if (arg == "--p2" && i + 1 < argc) {
            spP2 = std::atoi(argv[++i]);
        } else if (arg == "--budget" && i + 1 < argc) {
            spBudget = std::atoi(argv[++i]);
        }
    }
    if (doSelfplay) return runSelfplay(spGames, spP1, spP2, spBudget);

    // ---- Web 模式 ----
    std::string webRoot = resolveWebRoot();
    GameServer server(webRoot, port);
    if (!server.start()) {
        // Windows 子系统下没有控制台，用消息框给出明确错误
        std::string msg = "Failed to start Gomoku UI.\n\nweb root: " + webRoot +
                          "\n\nMake sure the 'web' folder sits next to the exe.";
        std::wstring wmsg(msg.begin(), msg.end());
        MessageBoxW(nullptr, wmsg.c_str(), L"Gomoku", MB_OK | MB_ICONERROR);
        return 1;
    }

    SystemBrowserLauncher launcher;
    const std::string url = server.url();
    printf("[info] Gomoku UI running at %s\n", url.c_str());
    fflush(stdout);   // 输出重定向到文件时也及时写出，便于脚本捕获端口
    if (openBrowser && !launcher.open(url)) {
        fprintf(stderr, "[warn] could not open browser automatically, visit %s\n",
                url.c_str());
    }

    server.wait();
    return 0;
}
// Windows 子系统入口：转发到 main。
// 编译为 Windows 子系统可让双击运行时完全不出现控制台窗口；
// --cli 模式再由 attachConsole() 按需附加控制台。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return main(__argc, __argv);
}
