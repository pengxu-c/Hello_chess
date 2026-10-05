// ============================================================
// app/ResourcePaths.cpp - 资源定位实现
//
// 【编译期锚点的作用】
//   GOMOKU_SOURCE_DIR 由 CMake 注入，是"构建时源码在哪"的权威答案。
//   有了它，源码根不再依赖任何猜测 —— 即使 exe 被拷到别的盘、
//   从任意目录启动，分层守卫照样能找到 contracts/。
//   运行时探测（exe 同级 / 上溯 / cwd）作为兜底，
//   用于「源码未随编译注入」的场景（例如手工用别的工具链编译）。
// ============================================================
#include "ResourcePaths.h"

#include <cstdio>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef GOMOKU_SOURCE_DIR
#define GOMOKU_SOURCE_DIR ""
#endif

namespace fs = std::filesystem;

namespace gomoku {
namespace {

bool isFile(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(fs::path(p), ec);
}

bool isDir(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(fs::path(p), ec);
}

// 目录里是否存在 index.html —— 判定"这是一个前端资源目录"的唯一标准
bool hasIndexHtml(const std::string& dir) {
    std::error_code ec;
    return isDir(dir) &&
           fs::exists(fs::path(dir) / "index.html", ec);
}

// 目录里是否同时含 contracts/ 与 kernel/ —— 判定"这是源码根"
bool looksLikeSourceRoot(const std::string& dir) {
    std::error_code ec;
    return isDir(dir) && fs::is_directory(fs::path(dir) / "contracts", ec) &&
           fs::is_directory(fs::path(dir) / "kernel", ec);
}

// 从 base 出发逐级上溯，最多 levels 层，返回第一个满足 pred 的目录。
// 用于"exe 在 build/Release 里，源码在 exe 上两级"这类布局。
template <typename Pred>
std::string searchUpward(const std::string& base, int levels, Pred pred) {
    fs::path cur = base;
    for (int i = 0; i <= levels && !cur.empty(); ++i) {
        const std::string s = cur.string();
        if (pred(s)) return s;
        if (!cur.has_parent_path() || cur.parent_path() == cur) break;
        cur = cur.parent_path();
    }
    return {};
}

}  // namespace

std::string executableDir() {
#ifdef _WIN32
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(),
                                           static_cast<DWORD>(buf.size()));
        if (n == 0) return ".";
        if (n < buf.size()) {
            std::wstring ws(buf.data(), n);
            const size_t pos = ws.find_last_of(L"\\/");
            if (pos == std::wstring::npos) return ".";
            return std::string(ws.begin(), ws.begin() + static_cast<long>(pos));
        }
        buf.resize(buf.size() * 2);   // 路径超长，重试
    }
#else
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && self.has_parent_path()) return self.parent_path().string();
    return ".";
#endif
}

std::string resolveWebRoot(const std::string& explicitRoot) {
    // 1) 显式指定优先：调用方知道自己在干什么，不做二次猜测
    if (!explicitRoot.empty()) return explicitRoot;

    // 2) 编译期锚点旁边（exe 与源码同树时的常见布局）
    const std::string src = GOMOKU_SOURCE_DIR;
    if (!src.empty() && hasIndexHtml(src + "/webapp")) return src + "/webapp";

    // 3) exe 同级：CMake 的 POST_BUILD 会把 webapp/ 复制到这里，
    //    因此"拷 exe 到任何目录"这一最常见用法在这一档就命中
    const std::string exe = executableDir();
    if (hasIndexHtml(exe + "/webapp")) return exe + "/webapp";
    if (hasIndexHtml(exe + "/web"))     return exe + "/web";   // 旧目录名

    // 4) 从 exe 逐级上溯：覆盖 build/Release → chess 这类多级布局。
    //    对每一级都试 "<该级>/webapp"，所以 build/Release 的 exe
    //    在上溯两级时正好命中 chess/webapp。
    {
        const std::string up = searchUpward(exe, 4, [](const std::string& dir) {
            return hasIndexHtml(dir + "/webapp") || hasIndexHtml(dir + "/web");
        });
        if (!up.empty()) return hasIndexHtml(up + "/webapp") ? up + "/webapp"
                                                             : up + "/web";
    }

    // 5) 当前工作目录：脚本从项目根直接调用时命中
    std::error_code ec;
    const std::string cwd = fs::current_path(ec).string();
    if (!ec && hasIndexHtml("webapp")) return "webapp";
    if (!ec && hasIndexHtml(cwd + "/webapp")) return cwd + "/webapp";

    return {};
}

std::string resolveSourceRoot() {
    // 1) 编译期锚点：最可靠，发布到任何位置都有效
    const std::string src = GOMOKU_SOURCE_DIR;
    if (!src.empty() && looksLikeSourceRoot(src)) return src;

    // 2) 从 exe 上溯：build/Release → chess
    const std::string up = searchUpward(executableDir(), 5, looksLikeSourceRoot);
    if (!up.empty()) return up;

    // 3) 从当前目录及其上级：CI 在项目根调用时命中
    std::error_code ec;
    const std::string cwd = fs::current_path(ec).string();
    if (!ec) {
        if (looksLikeSourceRoot(cwd)) return cwd;
        const std::string up2 = searchUpward(cwd, 3, looksLikeSourceRoot);
        if (!up2.empty()) return up2;
    }

    return {};
}

}  // namespace gomoku
