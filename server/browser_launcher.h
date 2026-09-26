// ============================================================
// browser_launcher.h - 浏览器启动抽象
// 默认实现用 ShellExecuteW 打开系统默认浏览器。
// 预留 WebView2 实现：将来新增一个子类即可，调用方不变。
// ============================================================
#pragma once
#include <string>

class BrowserLauncher {
public:
    virtual ~BrowserLauncher() = default;
    // 打开指定 URL，成功返回 true
    virtual bool open(const std::string& url) = 0;
};

// ---- 系统默认浏览器实现 ----
class SystemBrowserLauncher : public BrowserLauncher {
public:
    bool open(const std::string& url) override;
};