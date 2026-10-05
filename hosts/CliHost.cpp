// ============================================================
// hosts/CliHost.cpp - 终端宿主
//
// 结构非常短，因为一切都被推到插件里去了：
//   core   = MatchSession（内核）
//   view   = CliView（界面插件）
//   player = 玩家插件（由 id 从注册表取）
//   store  = 存储插件
// 宿主自己只剩下「主循环 + 收尾」。
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
    ViewContext vctx;
    vctx.headless = opts_.headless;
    std::unique_ptr<IView> view = createView("cli", vctx);
    if (!view) {
        std::fprintf(stderr, "cli view plugin unavailable\n");
        return 1;
    }
    session.addObserver(view.get());

    // ---- 3. 开一局（人类 vs AI 是默认组合） ----
    session.start();
    session.startGame(opts_.rules, opts_.black, opts_.white, opts_.storage);

    // ---- 4. 驱动界面（后台线程读键盘/推进回放），宿主只等内核结束 ----
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
