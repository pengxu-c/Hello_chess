// ============================================================
// main.cpp - 程序入口
// 默认进入 Web 模式：启动本地 HTTP 服务，并用系统浏览器打开界面。
// 传 --cli 参数则走原终端流程，便于脚本 / 调试 / 回归对照。
// ============================================================
#include "controller.h"
#include "server/game_server.h"
#include <cstdio>
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

int main(int argc, char** argv) {
    // 解析命令行参数：--cli 走终端流程；--port N 指定端口；--no-browser 不自动开浏览器
    int port = 0;
    bool openBrowser = true;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli") {
            return runCli();
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--no-browser") {
            openBrowser = false;
        }
    }

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
