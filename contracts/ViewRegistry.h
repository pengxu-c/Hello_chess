// ============================================================
// contracts/ViewRegistry.h - 前端界面插件注册表 + 创建上下文
//
// 【扩展点 2：新增前端界面】
//   1) 写一个类实现 gomoku::IView（render / poll 两个方法，notify 可选）
//   2) 在 plugins/ViewRegistry.cpp 的 registerBuiltinViews() 里加一行 add()
//   3) 在 CMakeLists.txt 的源文件列表里加上你那个 .cpp
//   完事 —— 用 --view <你的id> 就能启动它，内核与玩家插件一行都不改。
//
// 甚至可以在同一个进程里同时挂多个界面（内核支持多观察者），
// 例如「网页 + 日志文件」同时看同一局棋。
// ============================================================
#pragma once
#include "GameTypes.h"
#include "IView.h"
#include "PluginRegistry.h"
#include <memory>
#include <string>
#include <vector>

namespace gomoku {

// ---- 创建界面时的上下文 ----
struct ViewContext {
    std::string webRoot;   // 只对需要静态资源的界面（web）有意义，其它界面忽略
    int port = 0;          // 0 = 自动分配
    bool openBrowser = false;
    bool headless = false; // 无人值守（自对弈/CI）：界面不应等待输入
};

using ViewRegistry = PluginRegistry<IView, ViewInfo, ViewContext>;

ViewRegistry& viewRegistry();

// 注册全部内置界面插件 —— 唯一注册点，见 plugins/ViewRegistry.cpp
void registerBuiltinViews(ViewRegistry& reg);

std::vector<ViewInfo> viewCatalog();

std::unique_ptr<IView> createView(const std::string& id, const ViewContext& ctx);

}  // namespace gomoku
