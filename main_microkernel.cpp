// ============================================================
// main.cpp - 微内核版程序入口（Gomoku.exe）
//
// 入口只做两件事，一行棋规都没有：
//   1. 解析参数（app/Options）
//   2. 按参数挑一个宿主跑起来（hosts/Hosts）
//
// 「换玩法」= 换个宿主；「换界面」= --view；「换玩家」= --black/--white。
// 旧版引以为苦的"入口里塞满 if (mode == ...)"在这里彻底消失。
// ============================================================
#include "app/Options.h"
#include "hosts/Hosts.h"

#include <cstdio>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// 编译为 Windows 子系统（双击不弹控制台）后，stdout/stderr 可能根本不存在，
// 而 printf 到无效句柄会直接崩掉。因此分两步处理：
//
//   ensureStdio()        —— 任何输出之前调用。已经有了可用句柄（控制台或重定向）
//                           就什么都不做；否则尝试附加父控制台。
//                           绝不 AllocConsole —— 因为凭空造一个控制台会让
//                           "--list > file" 这类重定向在部分宿主下挂住。
//   ensureConsoleWindow() —— 只有"需要人在终端里交互"的模式（--cli / --selfplay）
//                           才在完全没有控制台时新开一个窗口，否则用户看不到输出。
void ensureStdio() {
#ifdef _WIN32
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        SetLastError(0);
        if (GetFileType(out) != FILE_TYPE_UNKNOWN || GetLastError() == ERROR_SUCCESS) {
            return;   // 已有控制台或被重定向：保持原样，绝不抢用户的输出
        }
    }
    AttachConsole(ATTACH_PARENT_PROCESS);   // 失败也不新建

    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
#else
    (void)0;
#endif
}

void ensureConsoleWindow() {
#ifdef _WIN32
    if (GetConsoleWindow() != nullptr) return;   // 已有控制台
    if (!AllocConsole()) return;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
#endif
}

#ifdef _WIN32
std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}
#endif

int runMain(int argc, char** argv) {
    ensureStdio();   // 必须先于任何 printf

#ifdef _WIN32
    // 源码是 UTF-8，控制台默认代码页会把中文打成乱码；这里把输出代码页切到 UTF-8。
    // 命令行下如果字体不支持中文，至少不会比原来更差。
    SetConsoleOutputCP(CP_UTF8);
#endif

    gomoku::Options opts;
    std::string error;
    if (!gomoku::parseOptions(argc, argv, opts, error)) {
        std::fprintf(stderr, "%s\n\n", error.c_str());
        gomoku::printPluginCatalog();
        return 2;
    }

    if (opts.listPlugins) {
        gomoku::printPluginCatalog();
        return 0;
    }

    // 交互式运行方式（终端下棋 / 自对弈）在完全没有控制台时新开一个窗口，
    // 否则用户看不到任何输出。
    if (opts.selfplay || opts.view == "cli") ensureConsoleWindow();

    // ---- 选宿主：这一张表就是"运行方式"的注册表 ----
    std::unique_ptr<gomoku::IAppHost> host;
    gomoku::WebHost* webHost = nullptr;

    if (opts.selfTest) {
        auto selfTest = std::make_unique<gomoku::SelfTestHost>(opts);
        selfTest->setHoldSeconds(opts.holdSeconds);
        host = std::move(selfTest);
    } else if (opts.selfplay) {
        host = std::make_unique<gomoku::SelfPlayHost>(opts);
    } else if (opts.view == "cli") {
        host = std::make_unique<gomoku::CliHost>(opts);
    } else {
        auto web = std::make_unique<gomoku::WebHost>(opts);
        webHost = web.get();
        host = std::move(web);
    }

    // ---- 启动提示：只往控制台写，不再弹 MessageBox ----
    //
    // 【为什么删掉弹窗】
    //   旧代码在"双击 exe"时弹一个「Gomoku 已启动」对话框，理由是当时
    //   Web 模式会把控制台藏起来，弹窗是用户唯一能看到网址的地方。
    //   现在控制台不再隐藏（藏起来会导致进程挂了都没人知道），
    //   地址就明明白白写在控制台上 —— 弹窗只剩打扰。
    //   同样的道理，参数错误与启动失败也只打印到控制台。
    if (webHost) {
        std::fprintf(stderr,
                     "\n提示：关闭这个控制台窗口即可退出程序。\n"
                     "若浏览器没有自动打开，请把上面显示的地址复制到浏览器。\n");
        std::fflush(stderr);
    }

    const int code = host->run();

#ifdef _WIN32
    if (webHost && code != 0) {
        std::fprintf(stderr,
                     "\n启动失败（退出码 %d）。请确认 webapp 目录与 Gomoku.exe "
                     "在同一目录下。\n",
                     code);
        std::fflush(stderr);
    }
#endif
    return code;
}

}  // namespace

int main(int argc, char** argv) {
    const int code = runMain(argc, argv);
#ifdef _WIN32
    // 双击运行时进程一结束控制台窗口就消失，最后的报错来不及看。
    // 只在"真的有一个会消失的控制台"且确实失败时等待回车。
    if (code != 0 && GetConsoleWindow() != nullptr &&
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR) {
        std::printf("\n[exit %d] Press Enter to close...", code);
        std::fflush(stdout);
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) {}
    }
#endif
    return code;
}

#ifdef _WIN32
// Windows 子系统入口：转发到 main（GUI 宿主下也可用 --cli）。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return runMain(__argc, __argv);
}
#endif
