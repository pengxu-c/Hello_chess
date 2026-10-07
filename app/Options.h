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
    // 默认界面：easyx（原生图形窗口）。
    // 想用其它界面：--view cli（终端）/ --view web（浏览器）。
    // 想改默认值就改这一行。
    std::string view = "easyx";         // 使用哪个界面插件（见 --views 列表）
    std::string black = "human";       // 黑方玩家插件 id
    std::string white = "minimax";     // 白方玩家插件 id
    RulesConfig rules;

    int  port = 0;                     // 0 = 自动分配
    bool openBrowser = true;
    bool storage = true;               // 是否启用记忆存储
    std::string dataDir = "data";
    int  thinkMs = 1500;               // AI 思考预算

    bool listPlugins = false;          // --list 打印玩家/界面目录后退出
    bool selfplay = false;             // --selfplay N：无头自对弈
    bool selfTest = false;             // --selftest：进程内端到端自检
    int  holdSeconds = 0;              // --hold N：自检后继续托管网页界面 N 秒（人工验收用）
    int  games = 1;
    bool autoStart = false;            // 启动后立即开局（默认等待用户点 New Game）
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
