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

    // ---- 不再在控制台询问"谁先手" ----
    // 双击启动要的是"打开就能玩"，多一句提问就多一次卡顿。
    // 想换先后手：网页里用选人下拉框，命令行用 --black / --white。
    // 默认 human 执黑先手，无需任何输入。

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
                     "\n  五子棋已启动，浏览器会自动打开棋盘；关闭这个控制台窗口即可退出。\n"
                     "  如果浏览器没弹出，把上面的网址复制到浏览器即可。\n\n");
        std::fflush(stderr);
    } else if (!opts.selfTest && !opts.selfplay) {
        std::fprintf(stderr,
                     "\n  五子棋  %s  黑:%s 白:%s  棋盘:%dx%d 连%d子\n"
                     "  关闭这个控制台窗口即可退出。\n\n",
                     opts.view.c_str(),
                     opts.black.c_str(),
                     opts.white.c_str(),
                     opts.rules.boardSize,
                     opts.rules.boardSize,
                     opts.rules.winLength);
        std::fflush(stderr);
    }

    int code = host->run();

#ifdef _WIN32
    // ---- 网页界面没起来时自动降级到终端 ----
    // webapp 缺失、端口被占、防火墙拦了本地监听，都不该让双击的用户
    // 只看到一个报错黑框。终端界面零依赖，一定能跑，所以作为最后兜底。
    // 只对"没显式指定界面"的情况降级：如果用户明确写了 --view web，
    // 就让他看到错误，而不是悄悄换成另一个界面。
    if (webHost && code != 0 && !opts.viewExplicit) {
        std::fprintf(stderr,
                     "\n  网页界面启动失败（退出码 %d），已自动切换到终端界面。\n"
                     "  若要继续用网页：确认 webapp/ 与 Gomoku.exe 同级，或用 --port 8099 换端口。\n\n",
                     code);
        std::fflush(stderr);
        opts.view = "cli";
        opts.openBrowser = false;
        host = std::make_unique<gomoku::CliHost>(opts);
        code = host->run();
    } else if (webHost && code != 0) {
        std::fprintf(stderr,
                     "\n启动失败（退出码 %d）。请确认 webapp 目录与 Gomoku.exe "
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
