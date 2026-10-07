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

    session.requestQuit();
    session.join(2000);
    return 0;
}

}  // namespace gomoku
