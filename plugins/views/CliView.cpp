// ============================================================
// plugins/views/CliView.cpp - 终端界面实现
// ============================================================
#include "CliView.h"

#include "../../contracts/PlayerRegistry.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace gomoku {
namespace {

// 把 "[A-Za-z][A-Za-z0-9_-]*" 的插件 id 渲染成人看的名字失败时的兜底
std::string seatLabel(const std::string& name, const std::string& id) {
    if (!name.empty()) return name;
    return id.empty() ? std::string("?") : id;
}

}  // namespace

void CliView::render(const ViewState& state) {
    last_ = state;
    if (state.version == drawnVersion_) return;   // 同一状态不重复刷屏
    drawnVersion_ = state.version;

    drawBoard(state);
}

void CliView::drawBoard(const ViewState& s) const {
    const int n = s.boardSize;
    std::string out;
    out.reserve(static_cast<size_t>(n) * (n * 2 + 4) + 256);

    out += "\n+";
    for (int i = 0; i < n * 2 + 1; ++i) out += '-';
    out += "+\n";

    // 列号（十位 + 个位），n<=30 所以两位数够用
    out += "   ";
    for (int c = 0; c < n; ++c) out += (c < 10 ? " " : "") + std::to_string(c) + " ";
    out += "\n";

    for (int r = 0; r < n; ++r) {
        char row[8];
        std::snprintf(row, sizeof(row), "%2d ", r);
        out += row;
        for (int c = 0; c < n; ++c) {
            const Stone v = s.at(r, c);
            char ch = '.';
            if (v == Stone::Black) ch = 'X';
            else if (v == Stone::White) ch = 'O';

            const bool isLast = (r == s.lastMove.r && c == s.lastMove.c);
            out += isLast ? '[' : ' ';
            out += ch;
            out += isLast ? ']' : ' ';
        }
        out += "\n";
    }

    out += "+";
    for (int i = 0; i < n * 2 + 1; ++i) out += '-';
    out += "+\n";

    std::printf("%s", out.c_str());
    std::printf("  %s | %d x %d, connect %d | moves %d | black: %s | white: %s\n",
                s.status.c_str(), s.boardSize, s.boardSize, s.winLength, s.moveCount,
                seatLabel(s.blackName, s.blackId).c_str(),
                seatLabel(s.whiteName, s.whiteId).c_str());

    if (s.thinking) {
        std::printf("  %s is thinking...\n",
                    (s.turn == Stone::Black ? "Black" : "White"));
    }
    if (!s.message.empty()) std::printf("  [%s]\n", s.message.c_str());
    std::fflush(stdout);
}

void CliView::notify(const std::string& message) {
    if (message.empty()) return;
    std::printf("  ! %s\n", message.c_str());
    std::fflush(stdout);
}

void CliView::printHelp() const {
    std::printf(
        "  commands:\n"
        "    r c        place a stone at row r column c (also accepts r,c)\n"
        "    u          undo one move\n"
        "    n          new game (same players)\n"
        "    s          save resume\n"
        "    l <id>     load resume by id\n"
        "    a          abort current game\n"
        "    t          toggle memory storage\n"
        "    b <id>     swap black player (see player ids)\n"
        "    w <id>     swap white player\n"
        "    h          this help\n"
        "    q          quit\n");
    std::fflush(stdout);
}

bool CliView::handleCommand(const std::string& line) {
    if (!session_) return true;

    // ---- 情况 1：整行就是坐标（"7 7" 或 "7,7"）----
    // 必须先于"取第一个词当命令"的判断，否则 "7 7" 会被当成未知命令 "7"。
    // （这个顺序错误曾在自检里表现为"输入坐标无效"。）
    {
        std::string normalized = line;
        for (char& ch : normalized)
            if (ch == ',' || ch == '\t') ch = ' ';
        std::istringstream ns(normalized);
        int r = -1, c = -1;
        std::string extra;
        if ((ns >> r >> c) && !(ns >> extra)) {
            session_->play(r, c);
            prompted_ = false;
            return true;
        }
    }

    // ---- 情况 2：命令字 ----
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    if (cmd.empty()) return true;

    if (cmd == "q" || cmd == "quit") return false;
    if (cmd == "h" || cmd == "help") { printHelp(); return true; }
    if (cmd == "u" || cmd == "undo") { session_->undo(1); return true; }
    if (cmd == "a" || cmd == "abort") { session_->abort(); return true; }
    if (cmd == "s" || cmd == "save") { session_->saveResume("from cli"); return true; }
    if (cmd == "t" || cmd == "storage") {
        session_->setStorageEnabled(!last_.storageEnabled);
        return true;
    }
    if (cmd == "n" || cmd == "new") {
        RulesConfig rules;
        rules.boardSize = last_.boardSize > 0 ? last_.boardSize : 15;
        rules.winLength = last_.winLength > 0 ? last_.winLength : 5;
        std::string b = last_.blackId.empty() ? "human" : last_.blackId;
        std::string w = last_.whiteId.empty() ? "human" : last_.whiteId;
        session_->startGame(rules, b, w, last_.storageEnabled);
        return true;
    }
    if (cmd == "l" || cmd == "load") {
        std::string id;
        iss >> id;
        if (id.empty()) std::printf("  usage: l <resume id>\n");
        else session_->loadResume(id);
        return true;
    }
    if (cmd == "b" || cmd == "black" || cmd == "w" || cmd == "white") {
        std::string id;
        iss >> id;
        if (id.empty()) {
            std::printf("  usage: %s <player id>   (try 'players' to list)\n", cmd.c_str());
            return true;
        }
        session_->swapPlayer((cmd == "b" || cmd == "black") ? Stone::Black : Stone::White, id);
        return true;
    }
    if (cmd == "players") {
        for (const auto& p : playerCatalog()) {
            std::printf("    %-16s %-14s %s%s\n", p.id.c_str(), p.display.c_str(),
                        p.description.c_str(), p.ready ? "" : "  [not configured]");
        }
        std::fflush(stdout);
        return true;
    }

    std::printf("  unknown command: %s (try 'h')\n", cmd.c_str());
    std::fflush(stdout);
    return true;
}

bool CliView::poll() {
    if (headless_) {
        // 无头：不读输入，只让出一点时间让控制线程推进
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return true;
    }
    if (!session_) return false;

    // 只有在"轮到人类"时才占用输入，避免 AI 思考时把用户的按键吃掉
    const bool myTurn = (last_.status == status::kInProgress) && last_.humanTurn &&
                        !last_.thinking;
    if (!myTurn) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return true;
    }

    if (!prompted_) {
        std::printf("  your move (r c), 'h' for help: ");
        std::fflush(stdout);
        prompted_ = true;
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
        // stdin 结束（管道 / Ctrl+Z）：优雅退出，不空转
        if (session_) session_->requestQuit();
        return false;
    }

    // 兼容 Windows 行尾：管道/重定向输入常常带 '\r'。
    // 不裁掉的话 "4 4\r" 会被解析成"三个 token"，坐标落子直接失效。
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                             line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    size_t begin = 0;
    while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) ++begin;

    if (begin >= line.size()) { prompted_ = false; return true; }
    return handleCommand(line.substr(begin));
}

}  // namespace gomoku
