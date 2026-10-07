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

// ============================================================
// 【本入口不碰控制台，一行都不碰】
//
//   本项目是**控制台子系统**（CMakeLists: WIN32_EXECUTABLE FALSE）。
//   这意味着 Windows 在启动 exe 的那一刻就已经：
//     * 分配好了一个控制台窗口；
//     * 把 stdout / stderr / stdin 全部接好。
//
//   跟用户以前写的任何一个 C++ 程序完全一样 —— 双击就弹黑框，
//   代码什么都不用做。早年这里的 AttachConsole / AllocConsole /
//   SetConsoleOutputCP 全是多余的，还会让"从 VS 启动"和"双击启动"
//   的窗口长得不一样，因此全部删除。
//
//   如果将来要改成 Windows 子系统（双击不弹框），那时才需要在入口
//   补回 AllocConsole + freopen("CONOUT$")，现在不需要。
// ============================================================

int runMain(int argc, char** argv) {
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

    // ---- 开局前问一句"谁执黑先手"（本地交互模式）----
    //
    // 【为什么要问】双击 exe 直接开打时，用户没法用命令行参数指定先后手，
    //   于是只能固定在 human 执黑。这里给一次选择机会，回车即用默认。
    //
    // 【哪些模式不问】
    //   * --selfplay / --selftest  无人值守，问了没人答
    //   * --headless               同上（CI / 脚本）
    //   * web                      网页界面自己有选玩家的控件，不劳控制台
    const bool interactive = !opts.selfTest && !opts.selfplay && !opts.headless;
    if (interactive && opts.view != "web") {
        std::fprintf(stderr,
                     "Who plays Black (moves first)?\n"
                     "  1) You       (human)        [default]\n"
                     "  2) Computer  (%s)\n"
                     "Enter 1 or 2, or just press Enter for default: ",
                     opts.black == "human" ? opts.white.c_str() : opts.black.c_str());
        std::fflush(stderr);

        char buf[64] = { 0 };
        if (std::fgets(buf, sizeof(buf), stdin) != nullptr && buf[0] == '2') {
            // 交换先后手：把 human 换到白方
            std::swap(opts.black, opts.white);
        }
        std::fprintf(stderr, "\n");
    }

    // ---- 选宿主：这一张表就是"运行方式"的注册表 ----
    std::unique_ptr<gomoku::IAppHost> host;
    gomoku::WebHost* webHost = nullptr;

    if (opts.selfTest) {
        auto selfTest = std::make_unique<gomoku::SelfTestHost>(opts);
        selfTest->setHoldSeconds(opts.holdSeconds);
        host = std::move(selfTest);
    } else if (opts.selfplay) {
        host = std::make_unique<gomoku::SelfPlayHost>(opts);
    } else if (opts.view == "web") {
        auto web = std::make_unique<gomoku::WebHost>(opts);
        webHost = web.get();
        host = std::move(web);
    } else {
        // 【这里只做一件事】把"本地窗口类界面"交给同一个宿主。
        //   哪个界面由 --view 决定，宿主内部 createView(opts_.view) 去注册表取。
        //
        //   【为什么只有 web 需要特殊对待】
        //     web 要开浏览器、要绑定端口、拿到真实端口后才能启动浏览器；
        //     其余界面（cli 终端、easyx 图形窗口…）都是本地窗口，
        //     不需要这些，因此共用一个宿主即可。
        //
        //   【这样改的价值：加新界面不用再动 main】
        //     以前这里是 `view == "cli" ? CliHost : WebHost`，
        //     任何非 cli 界面都会掉进 WebHost 分支 —— 加一个 easyx 就要改这里。
        //     现在新增界面只需在 plugins/ViewRegistry.cpp 注册一行。
        host = std::make_unique<gomoku::CliHost>(opts);
    }

    // ---- 启动提示：只往控制台写，不再弹 MessageBox ----
    //
    // 【为什么删掉弹窗】
    //   旧代码在"双击 exe"时弹一个「Gomoku 已启动」对话框，理由是当时
    //   Web 模式会把控制台藏起来，弹窗是用户唯一能看到网址的地方。
    //   现在控制台不再隐藏（藏起来会导致进程挂了都没人知道），
    //   地址就明明白白写在控制台上 —— 弹窗只剩打扰。
    //   同样的道理，参数错误与启动失败也只打印到控制台。
    //
    // 【为什么 EasyX 也必须打印】
    //   本地窗口类界面（easyx / cli）本身不在控制台上写任何东西，
    //   不打印的话双击后就是一个全黑的窗口，用户无法判断程序到底起来了没有 ——
    //   看起来跟崩溃一模一样。所以至少要说清"我在跑、怎么退出"。
    if (webHost) {
        std::fprintf(stderr,
                     "\n提示：关闭这个控制台窗口即可退出程序。\n"
                     "若浏览器没有自动打开，请把上面显示的地址复制到浏览器。\n");
        std::fflush(stderr);
    } else if (!opts.selfTest && !opts.selfplay) {
        std::fprintf(stderr,
                     "\n========================================\n"
                     "  Hello Chess  (%s)\n"
                     "========================================\n"
                     "  Black: %-14s  White: %s\n"
                     "  Board: %d x %d          Win: %d in a row\n"
                     "\n"
                     "  The game window is open now.\n"
                     "  Close this console window (or the game window) to quit.\n"
                     "\n"
                     "  Other frontends:\n"
                     "    hello_chess.exe --view cli     terminal UI\n"
                     "    hello_chess.exe --view web     browser UI\n"
                     "========================================\n\n",
                     opts.view.c_str(),
                     opts.black.c_str(),
                     opts.white.c_str(),
                     opts.rules.boardSize,
                     opts.rules.boardSize,
                     opts.rules.winLength);
        std::fflush(stderr);
    }

    const int code = host->run();

#ifdef _WIN32
    if (webHost && code != 0) {
        std::fprintf(stderr,
                     "\n启动失败（退出码 %d）。请确认 webapp 目录与 hello_chess.exe "
                     "在同一目录下。\n",
                     code);
        std::fflush(stderr);
    }
#endif
    return code;
}

int main(int argc, char** argv) {
    // 不做任何控制台判断、不等回车 —— 退出就退出，
    // 跟普通 C++ 程序一模一样。（早年的"按回车关闭"逻辑已删除。）
    return runMain(argc, argv);
}
