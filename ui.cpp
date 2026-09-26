// ============================================================
// ui.cpp - UI 类实现（纯文本终端）
// 用字符画棋盘（'.'=空 'X'=黑 'O'=白，小写 x/o 标记最后一手），
// 人类落子通过标准输入读入 "行 列" 坐标（0 起）。
// ============================================================
#include "ui.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <windows.h>
#include <conio.h>

UI::UI() {}
UI::~UI() {}

// 文本模式无需创建窗口，仅打印一次操作说明
void UI::initWindow(int w, int h) {
    (void)w; (void)h;
    printf("\n=== Gomoku (text mode) ===\n");
    printf("Enter your move as two numbers: row col (0-based), e.g. \"7 7\".\n\n");
}

void UI::close() {
    // 文本模式无窗口资源需要释放
}

// 文本模式不使用像素布局，仅记录棋盘尺寸用于输入范围校验
void UI::setLayout(int gridSize, int xOffset, int yOffset, int boardSize) {
    (void)gridSize; (void)xOffset; (void)yOffset;
    boardSize_ = boardSize;
}

// 宽字符转本地多字节编码（GBK），便于 printf 输出
std::string UI::toNarrow(const wchar_t* w) const {
    if (!w) return std::string();
    int len = WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// 非阻塞检测标准输入：有输入则读一行 "行 列" 并记为待落子坐标。
// 无输入时短暂休眠，避免人类回合空转占满 CPU。
void UI::pollMouse() {
    if (hasClick_) return;                 // 已有待处理输入
    if (!_kbhit()) { Sleep(20); return; }  // 无输入：休眠后返回

    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return;
    int r = -1, c = -1;
    if (sscanf_s(buf, "%d %d", &r, &c) == 2) {
        if (r >= 0 && r < boardSize_ && c >= 0 && c < boardSize_) {
            clickR_ = r;
            clickC_ = c;
            hasClick_ = true;
        } else {
            printf("Out of range: row/col must be 0..%d.\n", boardSize_ - 1);
        }
    } else {
        printf("Invalid input. Use \"row col\", e.g. 7 7.\n");
    }
}

// 非阻塞读取按键虚拟码（回放用）：ESC / Space / B / Home / End
int UI::pollKey() {
    if (!_kbhit()) return 0;
    int ch = _getch();
    if (ch == 0 || ch == 224) {            // 扩展键前缀
        if (_kbhit()) {
            int ext = _getch();
            if (ext == 71) return VK_HOME;
            if (ext == 79) return VK_END;
        }
        return 0;
    }
    if (ch == 27) return VK_ESCAPE;
    if (ch == ' ') return VK_SPACE;
    if (ch == 'b' || ch == 'B') return 'B';
    return ch;
}

Pos UI::hoverPos() const { return { -1, -1 }; }
Pos UI::clickPos() const { return { clickR_, clickC_ }; }
bool UI::hasClick() const { return hasClick_; }
void UI::clearClick() { hasClick_ = false; }

// 打印棋盘：'.'空位、'X'黑子、'O'白子；最后一手用小写 x/o 标记
void UI::drawBoard(const Board& board, Pos lastBlack, Pos lastWhite, ChessType turn) {
    int n = board.size();

    // 列号表头
    printf("    ");
    for (int c = 0; c < n; c++) printf("%2d", c);
    printf("\n");

    for (int r = 0; r < n; r++) {
        printf("%3d ", r);
        for (int c = 0; c < n; c++) {
            ChessType t = board.at(r, c);
            char ch = '.';
            if (t == ChessType::Black) ch = 'X';
            else if (t == ChessType::White) ch = 'O';
            // 最后一手用小写标记
            if (lastBlack.valid() && lastBlack.r == r && lastBlack.c == c) ch = 'x';
            else if (lastWhite.valid() && lastWhite.r == r && lastWhite.c == c) ch = 'o';
            printf("%2c", ch);
        }
        printf("\n");
    }

    if (turn == ChessType::Black)      printf("Turn: Black (X)\n");
    else if (turn == ChessType::White) printf("Turn: White (O)\n");
}

// 渲染一帧：仅当棋盘/最后一手/回合发生变化时才清屏重绘，避免闪烁
void UI::render(const Board& board, Pos hover, Pos lastBlack, Pos lastWhite, ChessType turn) {
    (void)hover;

    int n = board.size();
    std::vector<int> snap(static_cast<size_t>(n) * n);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            snap[static_cast<size_t>(r) * n + c] = static_cast<int>(board.at(r, c));

    bool same = (snap == snapshot_) &&
                lastBlack.r == lastB_.r && lastBlack.c == lastB_.c &&
                lastWhite.r == lastW_.r && lastWhite.c == lastW_.c &&
                turn == lastTurn_;
    if (same) return;

    snapshot_ = snap;
    lastB_ = lastBlack;
    lastW_ = lastWhite;
    lastTurn_ = turn;

    system("cls");
    drawBoard(board, lastBlack, lastWhite, turn);
}

void UI::messageBox(const wchar_t* text) {
    printf("\n>> %s\n", toNarrow(text).c_str());
}

int UI::askYesNo(const wchar_t* text) {
    printf("\n%s (y/n): ", toNarrow(text).c_str());
    char buf[16];
    if (fgets(buf, sizeof(buf), stdin)) {
        if (buf[0] == 'y' || buf[0] == 'Y') return IDYES;
    }
    return IDNO;
}
