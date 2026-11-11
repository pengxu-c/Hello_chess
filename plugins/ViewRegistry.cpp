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
#ifdef GOMOKU_HAS_EASYX
#include "views/EasyXView.h"
#endif

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

    // ---- EasyX 图形窗口：本地原生窗口，无需浏览器/端口 ----
    // 注意工厂里必须在这里 open()：IView 没有"生命周期钩子"，
    // 而窗口必须在构造完成后立刻创建，否则第一次 poll() 会在未初始化的
    // 绘图上下文里画东西。创建失败则返回 nullptr —— main_microkernel.cpp
    // 会自动把所有非 web 界面交给 CliHost，无需在这里或入口做任何判断。
    //
    // 【为什么整段要 #ifdef】EasyX 是可选依赖（头文件 + 静态库），
    // 没装 EasyX 的机器上这个插件根本编不出来；用宏包住后，
    // 其余界面与内核不受影响（宏由 CMake 在找到 easyx.h 时定义）。
#ifdef GOMOKU_HAS_EASYX
    reg.add(ViewInfo{ "easyx", "EasyX (window)",
                      "原生图形窗口，鼠标点击落子（无需浏览器）" },
            [](const ViewContext& ctx) -> std::unique_ptr<IView> {
                auto view = std::make_unique<EasyXView>(ctx.headless);
                if (!view->open()) return nullptr;
                return view;
            });
#endif

    // ==== 你的界面加在这里 ====
    // reg.add(ViewInfo{ "my-view", "My View", "一句话说明" },
    //         [](const ViewContext& ctx) -> std::unique_ptr<IView> {
    //             return std::make_unique<MyView>(ctx);
    //         });
    //
    // 加完就能用：`Gomoku.exe --view my-view`
    // main_microkernel.cpp 与 hosts/CliHost.cpp 都不需要改动。
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
