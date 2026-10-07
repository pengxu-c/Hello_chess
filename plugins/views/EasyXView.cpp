// ============================================================
// plugins/views/EasyXView.cpp - EasyX 图形窗口界面（复用 2026-09 旧版 UI 的画法）
//
// 【本文件的来历：为什么是"适配"而不是"重写"】
//   项目在 2026-09-26（提交 0ab024d「web前端时代」）之前，图形界面用的是
//   EasyX，代码在 ui.h / ui.cpp 里；那次重构把它删掉换成了 Web 界面。
//   用户要求把 EasyX 前端加回来。与其重写一套画棋盘的逻辑，不如把旧版
//   那套已经调好的绘制（配色、棋子描边、最后一手标记环、悬停圈）原样接回来：
//   engine/legacy_ui.{h,cpp} 就是从 git 里取回的原件，一行没改。
//
// 【这一层做什么】
//   旧 UI 面向旧引擎（Board / ChessType / Pos 三个旧类型），
//   新内核只给 ViewState 快照。所以本文件负责：
//     ViewState  → 旧 Board   （每次 render 转换一份，供旧 UI 画）
//     旧 Pos     → 新行坐标   （像素→格子换算仍由旧 UI 的 pixelToCell 做）
//   内核与旧引擎因此互不知情，各自保持完整。
//
// 【线程约束】
//   render() 由内核控制线程调用，poll() 由 ViewDriver 线程调用。
//   EasyX 绘图 API 不是线程安全的，因此：
//     render() —— 只把快照存下来（纯内存，安全）
//     poll()   —— 独占绘图 + 处理鼠标
//   代价是画面比状态晚一帧（几毫秒），交互上完全无感。
//
// 【本文件所有界面文字用英文】
//   EasyX 的 outtextxy 走 TCHAR 与系统字体，中文在不同机器上可能显示成
//   方块/问号。状态名本来就是英文（InProgress / BlackWin…），保持一致即可。
// ============================================================
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "EasyXView.h"

#include "../../engine/legacy_easyx/ui.h"   // 旧版 EasyX 界面（git 取回的原件）

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace gomoku {
namespace {

// ---- 新契约层类型 → 旧引擎类型 的转换 ----
inline ChessType toLegacy(Stone s) { return static_cast<ChessType>(static_cast<int>(s)); }
inline Stone fromLegacy(ChessType c) { return static_cast<Stone>(static_cast<int>(c)); }

constexpr COLORREF kStatusBg = RGB(240, 220, 180);   // 状态栏底色（沿用旧版）

// EasyX 窗口固定尺寸（用户指定 620 x 620）。
// 固定值而非按屏幕比例算：行为可预测，且任何分辨率下都能完整显示。
constexpr int kWindowSize = 620;

// std::string → EasyX 文本类型。
// 【为什么需要】本项目开了 UNICODE，于是 outtextxy 的形参 LPCTSTR
//   实际是 const wchar_t*，直接传 c_str()（const char*）会编译失败。
// 【为什么不逐字节填 TCHAR】那样中文会乱码：UTF-8 的一个汉字占 3 字节，
//   必须整体按编码解析，逐字节当 wchar_t 就废了。
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (need <= 1) return {};
    std::wstring out(static_cast<size_t>(need - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), need);
    return out;
}

// 把 UTF-8 的 std::string 交给 outtextxy；UNICODE / 非 UNICODE 都能编。
void putText(int x, int y, const std::string& utf8) {
#ifdef UNICODE
    outtextxy(x, y, widen(utf8).c_str());
#else
    outtextxy(x, y, utf8.c_str());
#endif
}

}  // namespace

EasyXView::EasyXView(bool headless)
    : headless_(headless), legacy_(new UI) {}

bool EasyXView::open() {
    if (open_) return true;

    // headless（自对弈 / CI）不建窗口：自动化场景不该弹窗出来。
    if (headless_) {
        open_ = true;
        return true;
    }

    // 【关键：这里不建窗口，真正的 initgraph 推迟到第一次 poll()】
    //
    //   原因：EasyX（以及所有 Win32 窗口）的**消息循环必须和创建窗口在
    //   同一个线程**。initgraph 在 A 线程建窗口，peekmessage 却在 B 线程读，
    //   就永远读不到鼠标/键盘消息 —— 表现正是"棋盘画得出来、但点了没反应"。
    //
    //   而本项目的 poll() 是由 ViewDriver 在**后台线程**调用的，
    //   如果 open() 在主线程就把窗口建了，两者就分处不同线程。
    //   所以真正的建窗动作放在 ensureWindow() 里，由 poll() 首次触发，
    //   保证"建窗"和"读消息"都在 ViewDriver 线程上。
    open_ = true;
    return true;
}

// 真正创建窗口 —— 必须且只能在 poll() 所在线程调用一次。
void EasyXView::ensureWindow() {
    if (windowReady_ || headless_) return;

    // ---- 窗口固定 620 x 620 ----
    //
    // 【为什么固定】用户明确要求 EasyX 模式用 620x620。
    //   早期版本"按屏幕比例算"，结果每次分辨率不同窗口就变一次大小，
    //   而且在 1080p 上算出过 1290 高的窗口导致 EasyX 段错误。
    //   固定尺寸后行为完全可预测，620 在任何屏幕上都能完整显示。
    //
    // 【布局】棋盘在窗口内居中，格子大小自动适配棋盘路数：
    //   留白 margin 上下左右对称，底部留 statusH 给状态栏。
    const int winW = kWindowSize;
    const int winH = kWindowSize;
    const int statusH = 110;
    const int margin = 26;

    // 可用正方形区域 = 窗口减去留白和状态栏
    const int avail = std::min(winW - margin * 2, winH - margin * 2 - statusH);
    int g = avail / std::max(1, boardSize_ - 1);
    if (g < 12) g = 12;   // 下限：再小棋子就看不清了

    // 棋盘居中
    const int boardPixels = g * (boardSize_ - 1);
    const int xOffset = std::max(margin, (winW - boardPixels) / 2);
    const int yOffset = std::max(margin, (winH - statusH - boardPixels) / 2);

    grid_ = g; xOffset_ = xOffset; marginY_ = yOffset;

    legacy_->initWindow(winW, winH);
    legacy_->setLayout(g, xOffset, yOffset, boardSize_);
    windowReady_ = true;
}

void EasyXView::close() {
    if (!windowReady_ || headless_) { open_ = false; windowReady_ = false; return; }
    legacy_->close();
    open_ = false;
    windowReady_ = false;
}

// 析构兜底：若宿主异常退出而没调 close()，别把 EasyX 窗口留在屏幕上。
EasyXView::~EasyXView() {
    if (windowReady_ && !headless_ && legacy_) legacy_->close();
}

// 底部状态栏：状态 / 双方 / 步数 / 轮到谁 / 一次性提示。
// 【用英文的原因】EasyX 的 outtextxy 走系统字体，中文在不同机器上可能
//   显示为方块或问号；状态名本来就是英文（InProgress / BlackWin…），保持一致。
void EasyXView::drawStatus() {
    const int y = marginY_ + grid_ * (boardSize_ - 1) + 24;

    setbkcolor(kStatusBg);
    settextcolor(BLACK);
    settextstyle(18, 0, L"Arial");
    putText(xOffset_, y, last_.status);

    char line[200];
    std::snprintf(line, sizeof(line), "%s (Black)  vs  %s (White)    %dx%d    win %d    move %d",
                  last_.blackName.empty() ? "?" : last_.blackName.c_str(),
                  last_.whiteName.empty() ? "?" : last_.whiteName.c_str(),
                  last_.boardSize, last_.boardSize, last_.winLength, last_.moveCount);
    settextstyle(15, 0, L"Arial");
    putText(xOffset_, y + 26, line);

    settextcolor(last_.thinking ? RGB(160, 40, 40) : BLACK);
    const char* tip = last_.thinking ? "thinking..."
                    : (last_.humanTurn ? "your move - click the board to place a stone"
                                       : "");
    std::snprintf(line, sizeof(line), "Turn: %s   %s",
                  last_.turn == Stone::Black ? "Black" : "White", tip);
    putText(xOffset_, y + 50, line);

    if (hasMessage_) {
        settextcolor(RGB(160, 40, 40));
        putText(xOffset_, y + 74, pendingMessage_);
    }
}

void EasyXView::render(const ViewState& state) {
    // 只存快照 + 同步棋盘；不碰绘图（见文件头线程约束说明）
    last_ = state;

    // 把 ViewState 摊成旧引擎的 Board，旧 UI 才能照常画
    legacyBoard_.resize(last_.boardSize);
    for (int r = 0; r < last_.boardSize; ++r)
        for (int c = 0; c < last_.boardSize; ++c)
            legacyBoard_.set(r, c, toLegacy(last_.at(r, c)));
}

void EasyXView::notify(const std::string& message) {
    if (message.empty()) return;
    pendingMessage_ = message;
    hasMessage_ = true;
}

bool EasyXView::poll() {
    if (!open_) return true;

    if (headless_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return true;
    }

    // 【必须在这里建窗】initgraph 与后面的 peekmessage 要在同一线程，
    // 否则读不到任何鼠标/键盘消息（表现为"画得出、点不动"）。
    // 详见 open() 与 ensureWindow() 里的说明。
    ensureWindow();
    if (!windowReady_) return true;

    // ---- 键盘：q / ESC 退出 ----
    // 放这里而不是宿主：界面才知道自己的"退出键"是什么。
    if (const int key = legacy_->pollKey(); key != 0) {
        if (key == 'Q' || key == VK_ESCAPE) return false;
    }

    // ---- 鼠标：移动更新悬停，左键落子 ----
    legacy_->pollMouse();

    // ---- 绘图（独占本线程，不与 render 并发）----
    Pos hover = legacy_->hasClick() ? Pos{ -1, -1 } : legacy_->hoverPos();
    const ChessType turn = toLegacy(last_.turn);
    legacy_->render(legacyBoard_, hover,
                    Pos{ last_.lastBlack.r, last_.lastBlack.c },
                    Pos{ last_.lastWhite.r, last_.lastWhite.c },
                    turn);

    drawStatus();

    // ---- 消费一次性提示 ----
    hasMessage_ = false;
    pendingMessage_.clear();

    // ---- 落子：只发给内核，界面自己绝不改棋盘 ----
    if (legacy_->hasClick()) {
        const Pos p = legacy_->clickPos();
        legacy_->clearClick();
        const bool canClick = last_.status == status::kInProgress &&
                              last_.humanTurn && !last_.thinking;
        if (canClick && p.valid() && session_) {
            session_->play(p.r, p.c);
        }
    }
    return true;
}

}  // namespace gomoku