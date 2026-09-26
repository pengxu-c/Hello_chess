// ============================================================
// browser_launcher.cpp - 浏览器启动实现
// 用 ShellExecuteW 调起系统默认浏览器打开本地服务地址。
// ============================================================
#include "browser_launcher.h"
#include <windows.h>
#include <string>

bool SystemBrowserLauncher::open(const std::string& url) {
    // URL 只含 ASCII（127.0.0.1 + 端口），逐字节转宽字符即可
    std::wstring wurl(url.begin(), url.end());
    HINSTANCE h = ShellExecuteW(nullptr, L"open", wurl.c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
    // ShellExecuteW 返回值大于 32 表示成功
    return reinterpret_cast<INT_PTR>(h) > 32;
}