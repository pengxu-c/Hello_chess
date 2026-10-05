// ============================================================
// plugins/ViewRegistry.cpp - 界面插件注册：全项目唯一的界面注册点
// ============================================================
//
//  ██  新增一个前端界面，只需要动这个文件一处  ██
//
//  步骤：
//    1) 写一个类实现 gomoku::IView（render + poll 两个方法）
//    2) 在下面 registerBuiltinViews() 里加一行 reg.add(...)
//    3) 把新 .cpp 加进 CMakeLists.txt
//
//  加完之后：`Gomoku.exe --view <你的id>` 就能用它启动，
//  内核、玩家插件、存储插件都不需要重新编译或修改。
//
//  同一个进程里可以同时挂多个界面（内核支持多观察者），例如
//  "网页 + 终端日志" 同时观察同一局棋。
//
#include "../contracts/ViewRegistry.h"
#include "views/CliView.h"
#include "views/WebView.h"

namespace gomoku {

void registerBuiltinViews(ViewRegistry& reg) {
    // ---- 终端界面：最轻、无依赖，也是 CI/无头场景的默认选择 ----
    reg.add(ViewInfo{ "cli", "CLI (terminal)",
                      "控制台文本棋盘，键盘输入坐标落子" },
            [](const ViewContext& ctx) -> std::unique_ptr<IView> {
                return std::make_unique<CliView>(ctx.headless);
            });

    // ---- 浏览器界面：内核状态以 SSE 推送到网页 ----
    reg.add(ViewInfo{ "web", "Web (browser)",
                      "本地 HTTP 服务 + 浏览器图形界面" },
            [](const ViewContext& ctx) -> std::unique_ptr<IView> {
                auto view = std::make_unique<WebView>(ctx.webRoot, ctx.port,
                                                      ctx.openBrowser, ctx.headless);
                // 端口绑定要在创建后立刻做：宿主需要拿到真实端口才能开浏览器
                if (!view->start()) return nullptr;
                return view;
            });

    // ==== 你的界面加在这里 ====
    // reg.add(ViewInfo{ "my-view", "My View", "一句话说明" },
    //         [](const ViewContext& ctx) -> std::unique_ptr<IView> {
    //             return std::make_unique<MyView>(ctx);
    //         });
}

ViewRegistry& viewRegistry() {
    static ViewRegistry reg = [] {
        ViewRegistry r;
        registerBuiltinViews(r);
        return r;
    }();
    return reg;
}

std::vector<ViewInfo> viewCatalog() {
    return viewRegistry().catalog();
}

std::unique_ptr<IView> createView(const std::string& id, const ViewContext& ctx) {
    return viewRegistry().create(id, ctx);
}

}  // namespace gomoku
