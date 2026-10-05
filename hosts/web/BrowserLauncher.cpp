// ============================================================
// hosts/web/BrowserLauncher.cpp
// ============================================================
#include "BrowserLauncher.h"

#include <windows.h>

namespace gomoku {

bool openInSystemBrowser(const std::string& url) {
    // URL 只含 ASCII（127.0.0.1 + 端口），逐字节转宽字符即可
    const std::wstring wurl(url.begin(), url.end());
    const HINSTANCE h = ShellExecuteW(nullptr, L"open", wurl.c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
    // ShellExecuteW 返回值 > 32 表示成功
    return reinterpret_cast<INT_PTR>(h) > 32;
}

}  // namespace gomoku
