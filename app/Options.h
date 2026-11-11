// ============================================================
// app/Options.h - 命令行参数（宿主共享）
//
// 参数解析被抽出来单独一处，是为了让「宿主」也能当插件：
// 新增一种运行方式（比如 --server 集群模式）时，只需要在解析里加字段，
// 再由新的 Host 决定怎么用，不必改已有宿主。
// ============================================================
#pragma once
#include "../contracts/GameTypes.h"
#include <string>

namespace gomoku {

struct Options {
    // 默认界面：浏览器。它不依赖任何额外图形库，双击即开，交互最完整。
    // 旧版曾以 EasyX 原生窗口为默认，但 VS 升级到 18 2026 后集成的
    // easyx.h 随旧实例一起消失；装好 EasyX 后仍可 --view easyx 显式启用。
    // 想用别的界面：--view cli（终端）/ --view easyx（原生窗口）。
    std::string view = "web";           // 使用哪个界面插件（见 --views 列表）
    bool viewExplicit = false;         // 用户是否显式写了 --view（决定能否自动降级）
    std::string black = "human";       // 黑方玩家插件 id
    std::string white = "minimax";     // 白方玩家插件 id
    RulesConfig rules;

    int  port = 0;                     // 0 = 自动分配
    bool openBrowser = true;
    bool storage = true;               // 是否启用记忆存储
    std::string dataDir = "data";
    int  thinkMs = 800;                // AI 思考预算（默认800ms，保证点击后很快响应）

    bool listPlugins = false;          // --list 打印玩家/界面目录后退出
    bool selfplay = false;             // --selfplay N：无头自对弈
    bool selfTest = false;             // --selftest：进程内端到端自检
    int  holdSeconds = 0;              // --hold N：自检后继续托管网页界面 N 秒（人工验收用）
    int  games = 1;
    bool autoStart = true;             // 启动即开局；--no-start 可回到"打开后手动点新局"
    bool quiet = false;                // 少打印

    // --cli / --headless 等便捷开关的原始意图
    bool wantCli = false;
    bool headless = false;
};

// 解析 argv；出错时返回 false 并填充 error。
bool parseOptions(int argc, char** argv, Options& out, std::string& error);

// 旧命令行用玩家编号（1..7）选择棋手；这里翻译成新插件 id，
// 让老脚本 / 老文档里的命令继续可用（兼容层只有这一个函数）。
std::string legacyPlayerId(int number);

// 打印玩家插件目录 / 界面插件目录（--list）
void printPluginCatalog();

}  // namespace gomoku
