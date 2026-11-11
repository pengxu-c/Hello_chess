// ============================================================
// hosts/CliHost.cpp - 本地窗口宿主（终端 / EasyX 等所有本地界面）
//
// 结构非常短，因为一切都被推到插件里去了：
//   core   = MatchSession（内核）
//   view   = 界面插件（由 opts_.view 决定用哪个）
//   player = 玩家插件（由 id 从注册表取）
//   store  = 存储插件
// 宿主自己只剩下「主循环 + 收尾」。
//
// 【它为什么叫 CliHost 却服务 EasyX】
//   本宿主服务的是「不需要开浏览器的本地交互界面」这一类：
//   终端界面、EasyX 图形窗口都属此类 —— 都是本地窗口、都由 ViewDriver 驱动、
//   都不需要绑定端口或开浏览器。真正需要开浏览器的是 WebHost，它独立成类。
//
//   【本文件是加新界面的关键：宿主不认界面，只认 id】
//   下面这一行用的是 opts_.view（用户传的 id），而不是写死的 "cli"：
//         createView(opts_.view, vctx)
//   正因如此，新增一个界面只需在 ViewRegistry.cpp 注册一行，
//   main_microkernel.cpp 与本文件都不需要改动。
// ============================================================
#include "Hosts.h"

#include "../app/ViewDriver.h"
#include "../contracts/PlayerRegistry.h"
#include "../contracts/ViewRegistry.h"
#include "../kernel/MatchSession.h"
#include "../plugins/storage/LegacyStorageGateway.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace gomoku {

int CliHost::run() {
    // ---- 1. 组装：存储插件 → 内核 ----
    auto storage = makeLegacyStorageGateway(opts_.dataDir);
    MatchSession session(std::move(storage));

    // ---- 2. 组装：界面插件 → 挂到内核 ----
    // 用 opts_.view 而非写死 "cli"：同一个宿主服务所有本地窗口界面。
    ViewContext vctx;
    vctx.headless = opts_.headless;
    vctx.port = opts_.port;
    std::unique_ptr<IView> view = createView(opts_.view, vctx);

    // 【失败要能降级，不能直接退出】
    //   默认界面是 easyx，而 EasyX 是可选依赖：没装 easyx.h 时该插件根本不存在，
    //   机器没有图形环境时 open() 也会失败。以前这里直接 return 1，
    //   表现为"双击没反应、只闪一下控制台"，用户完全不知道发生了什么。
    //   现在：首选界面不可用 → 退回终端界面继续玩 → 只有终端也起不来才失败。
    if (!view && opts_.view != "cli") {
        std::fprintf(stderr,
                     "[view] '%s' is unavailable; falling back to the terminal UI (cli).\n"
                     "[view] Use --view <id> to pick another view, or --views to list them.\n",
                     opts_.view.c_str());
        std::fflush(stderr);
        view = createView("cli", vctx);
    }
    if (!view) {
        std::fprintf(stderr, "view plugin '%s' unavailable\n", opts_.view.c_str());
        std::fprintf(stderr, "use --views to list the available view plugins\n");
        return 1;
    }
    session.addObserver(view.get());

    // ---- 3. 开一局（人类 vs AI 是默认组合） ----
    session.start();
    session.startGame(opts_.rules, opts_.black, opts_.white, opts_.storage);

    // ---- 4. 驱动界面（后台线程读键盘/鼠标/推进回放），宿主只等内核结束 ----
    ViewDriver driver(*view, &session);
    driver.start();
    while (driver.running() && !session.quitting()) {
        session.join(50);
    }
    driver.stop();

    // ---- 收尾顺序（UAF窗口B）----
    // controlLoop的dispatchNotifications会在锁外用observers_里的裸指针render，
    // 而局部变量按声明逆序析构时view先于session销毁。必须先摘观察者、请求退出，
    // 再循环join到控制线程真正结束，才能确认锁外render已完成、不会再碰view。
    session.removeObserver(view.get());
    session.requestQuit();
    // finished_仅在controlLoop末尾（或异常兜底）置位，join只在它为真时才真join。
    // 循环到finished_为真，总上限50×200ms=10s避免异常情况下死等。
    for (int i = 0; i < 50 && !session.finished(); ++i) {
        session.join(200);
    }
    session.join(2000);   // 真正回收joinable线程，幂等
    return 0;
}

}  // namespace gomoku
