// ============================================================
// app/Options.cpp - 参数解析
// ============================================================
#include "Options.h"

#include "../contracts/PlayerRegistry.h"
#include "../contracts/ViewRegistry.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace gomoku {
namespace {

// 便捷解析：--key value
bool takeValue(int argc, char** argv, int& i, const char* key, std::string& out) {
    const std::string arg = argv[i];
    const std::string want = std::string("--") + key;
    if (arg == want) {
        if (i + 1 >= argc) return false;
        out = argv[++i];
        return true;
    }
    if (arg.rfind(want + "=", 0) == 0) {
        out = arg.substr(want.size() + 1);
        return true;
    }
    return false;
}

int toInt(const std::string& s, int fallback) {
    try {
        std::size_t used = 0;
        const int v = std::stoi(s, &used);
        // 拒绝 "12abc" 这类部分解析：必须整串都被消费。
        if (used != s.size()) return fallback;
        return v;
    } catch (...) {
        return fallback;
    }
}

}  // namespace

std::string legacyPlayerId(int number) {
    switch (number) {
        case 1: return "human";
        case 2: return "easy-judge";
        case 3: return "puregreed-1.0";
        case 4: return "puregreed-1.1";
        case 5: return "minimax";
        case 6: return "api-ai";
        case 7: return "tactical-max";
        default: return "minimax";
    }
}

bool parseOptions(int argc, char** argv, Options& out, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string v;

        if (arg == "--help" || arg == "-h") { out.listPlugins = true; continue; }
        if (arg == "--cli") { out.wantCli = true; out.view = "cli"; out.viewExplicit = true; continue; }
        if (arg == "--headless") { out.headless = true; out.openBrowser = false; continue; }
        if (arg == "--no-browser") { out.openBrowser = false; continue; }
        if (arg == "--no-storage") { out.storage = false; continue; }
        if (arg == "--quiet") { out.quiet = true; continue; }
        // 默认已经自动开局；保留 --start 兼容旧脚本，另给 --no-start 关闭。
        if (arg == "--start") { out.autoStart = true; continue; }
        if (arg == "--no-start") { out.autoStart = false; continue; }

        if (takeValue(argc, argv, i, "view", v))   { out.view = v; out.viewExplicit = true; out.wantCli = (v == "cli"); continue; }
        if (takeValue(argc, argv, i, "black", v))  { out.black = v; continue; }
        if (takeValue(argc, argv, i, "white", v))  { out.white = v; continue; }
        if (takeValue(argc, argv, i, "board", v))  { out.rules.boardSize = toInt(v, 15); continue; }
        if (takeValue(argc, argv, i, "win", v))    { out.rules.winLength = toInt(v, 5); continue; }
        if (takeValue(argc, argv, i, "port", v))   { out.port = toInt(v, 0); continue; }
        if (takeValue(argc, argv, i, "data", v))   { out.dataDir = v; continue; }
        if (takeValue(argc, argv, i, "think", v))  { out.thinkMs = toInt(v, 1500); continue; }

        // 兼容旧命令行：--p1 / --p2 是玩家编号，这里翻译成插件 id
        if (takeValue(argc, argv, i, "p1", v)) {
            out.black = legacyPlayerId(toInt(v, 1));
            out.selfplay = true;
            continue;
        }
        if (takeValue(argc, argv, i, "p2", v)) {
            out.white = legacyPlayerId(toInt(v, 1));
            out.selfplay = true;
            continue;
        }
        if (takeValue(argc, argv, i, "budget", v)) {
            out.thinkMs = toInt(v, 1500);
            continue;
        }

        if (arg == "--selfplay") {
            out.selfplay = true;
            out.view = "cli";
            out.headless = true;
            out.openBrowser = false;
            // 允许 "--selfplay 20" 这种带局数的写法
            if (i + 1 < argc && argv[i + 1][0] != '-') out.games = toInt(argv[++i], 1);
            continue;
        }
        if (arg == "--list") { out.listPlugins = true; continue; }
        // 【--views】帮助文本一直在推荐它（printPluginCatalog 的 Examples 段），
        //   但解析器里从来没有这一支 —— 照着帮助敲会得到 "unknown argument"。
        //   语义上与 --list 一致（都打印插件目录），因此直接复用。
        if (arg == "--views") { out.listPlugins = true; continue; }
        if (arg == "--selftest") { out.selfTest = true; out.headless = true; continue; }
        if (takeValue(argc, argv, i, "hold", v)) { out.holdSeconds = toInt(v, 0); continue; }
        error = "unknown argument: " + arg;
        return false;
    }

    if (out.games < 1) out.games = 1;
    // 越界参数回到安全默认：端口、思考预算、托管秒数都不能为负或超范围。
    if (out.port < 0 || out.port > 65535) out.port = 0;
    if (out.thinkMs < 0) out.thinkMs = 0;
    if (out.holdSeconds < 0) out.holdSeconds = 0;
    out.rules = out.rules.normalized();
    return true;
}

void printPluginCatalog() {
    std::printf("Player plugins (use with --black <id> / --white <id>):\n");
    for (const auto& p : playerCatalog()) {
        std::printf("  %-16s %-14s %s%s\n", p.id.c_str(), p.display.c_str(),
                    p.description.c_str(), p.ready ? "" : "  [not configured]");
    }
    std::printf("\nView plugins (use with --view <id>):\n");
    for (const auto& v : viewCatalog()) {
        std::printf("  %-16s %-14s %s\n", v.id.c_str(), v.display.c_str(),
                    v.description.c_str());
    }
    std::printf(
        "\nExamples:\n"
        "  Gomoku.exe                                   # browser UI (default), auto-start\n"
        "  Gomoku.exe --no-start                        # open without starting a game\n"
        "  Gomoku.exe --view easyx                      # native window (needs EasyX)\n"
        "  Gomoku.exe --view cli                        # terminal UI\n"
        "  Gomoku.exe --views                           # list all view plugins\n"
        "  Gomoku.exe --selfplay 2 --black tactical-max --white minimax --think 800\n");
}

}  // namespace gomoku
