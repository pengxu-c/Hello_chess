// ============================================================
// hosts/web/BrowserLauncher.h - 用系统默认浏览器打开 URL
//
// 很小的一块，但刻意独立出来：将来要改成"内嵌 WebView2 窗口"
// 只需另写一个实现，宿主代码不动。
// ============================================================
#pragma once
#include <string>

namespace gomoku {

bool openInSystemBrowser(const std::string& url);

}  // namespace gomoku
