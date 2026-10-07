// ============================================================
// hosts/WebHost.cpp - 浏览器宿主
//
// 与 CliHost 的唯一区别：它创建的界面插件是 web。
// —— 这正是微内核要证明的事：换界面不需要内核知道。
// ============================================================
#include "Hosts.h"

#include "../app/ResourcePaths.h"
#include "../app/ViewDriver.h"
#include "../contracts/ViewRegistry.h"
#include "../kernel/MatchSession.h"
#include "../plugins/storage/LegacyStorageGateway.h"
#include "../plugins/views/WebView.h"
#include "web/BrowserLauncher.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace gomoku {

int WebHost::run() {
    // 前端资源定位统一交给 ResourcePaths（编译期源码锚点 + exe 上溯 + cwd），
    // 避免"宿主和自检各写一份探测"导致同一份代码换个目录就自检失败。
    const std::string webRoot = resolveWebRoot();
    if (webRoot.empty()) {
        std::fprintf(stderr,
                     "cannot find webapp/index.html.\n"
                     "expected a 'webapp' folder next to hello_chess.exe "
                     "(or up to 4 levels above it).\n");
        return 1;
    }

    // ---- 组装：存储插件 → 内核 ----
    auto storage = makeLegacyStorageGateway(opts_.dataDir);
    MatchSession session(std::move(storage));

    // ---- 组装：界面插件（web）→ 挂到内核 ----
    ViewContext vctx;
    vctx.webRoot = webRoot;
    vctx.port = opts_.port;
    vctx.openBrowser = opts_.openBrowser;
    vctx.headless = opts_.headless;

    std::unique_ptr<IView> view = createView("web", vctx);
    if (!view) {
        std::fprintf(stderr, "failed to start the web view (port %d busy?)\n", opts_.port);
        return 1;
    }
    session.addObserver(view.get());

    // 真实端口只有在绑定成功后才能知道，因此这里回读一次
    auto* web = dynamic_cast<WebView*>(view.get());
    const std::string url = web ? web->url()
                                : ("http://127.0.0.1:" + std::to_string(opts_.port) + "/");

    std::printf("[info] Gomoku microkernel UI at %s\n", url.c_str());
    std::fflush(stdout);   // 重定向到文件时也及时写出，便于脚本读取端口

    if (opts_.openBrowser && web) openInSystemBrowser(url);

    // 通知入口：服务已就绪（双击启动时入口会据此弹一个可见提示）
    if (ready_) ready_(url, opts_.port <= 0);

    // ---- 起步：不自动开局，让用户在网页里选人（--start 可自动开局） ----
    session.start();
    if (opts_.autoStart) {
        session.startGame(opts_.rules, opts_.black, opts_.white, opts_.storage);
    }

    // ---- 驱动界面输入侧：统一走 ViewDriver，宿主不再自己写主循环 ----
    ViewDriver driver(*view, &session);
    driver.start();

    while (driver.running() && !session.quitting()) {
        session.join(20);
    }
    driver.stop();

    session.requestQuit();
    session.join(2000);
    return 0;
}

}  // namespace gomoku
