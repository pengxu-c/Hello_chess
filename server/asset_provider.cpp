// ============================================================
// asset_provider.cpp - 前端静态资源提供者实现
// DiskAssetProvider 把 URL 路径映射到 web 根目录下的磁盘文件，
// 读取内容并按扩展名给出 MIME，供 HTTP 服务直接返回。
// ============================================================
#include "asset_provider.h"
#include <fstream>
#include <sstream>
#include <cstring>

DiskAssetProvider::DiskAssetProvider(std::string rootDir)
    : rootDir_(std::move(rootDir)) {
    // 去掉尾部斜杠，避免拼接出 "web//index.html"
    while (!rootDir_.empty() &&
           (rootDir_.back() == '/' || rootDir_.back() == '\\')) {
        rootDir_.pop_back();
    }
}

// 拒绝含 ".." 或反斜杠的路径，防止目录穿越读取根目录之外的任意文件
bool DiskAssetProvider::isPathSafe(const std::string& urlPath) {
    if (urlPath.find("..") != std::string::npos) return false;
    if (urlPath.find('\\') != std::string::npos) return false;
    return true;
}

// 按扩展名推断 MIME 类型；未知类型按二进制流处理
std::string DiskAssetProvider::guessMime(const std::string& path) {
    auto endsWith = [&path](const char* ext) {
        size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (endsWith(".html")) return "text/html; charset=utf-8";
    if (endsWith(".css"))  return "text/css; charset=utf-8";
    if (endsWith(".js"))   return "application/javascript; charset=utf-8";
    if (endsWith(".json")) return "application/json; charset=utf-8";
    if (endsWith(".svg"))  return "image/svg+xml";
    if (endsWith(".png"))  return "image/png";
    if (endsWith(".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

// 以二进制方式整体读取文件；文件不存在或不可读返回 false
bool DiskAssetProvider::readFile(const std::string& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

bool DiskAssetProvider::ready() const {
    std::string dummy;
    return readFile(rootDir_ + "/index.html", dummy);
}

bool DiskAssetProvider::get(const std::string& urlPath,
                            std::string& content,
                            std::string& mime) const {
    if (!isPathSafe(urlPath)) return false;

    // "/" 或空路径默认返回首页
    std::string rel = urlPath;
    if (rel.empty() || rel == "/") rel = "/index.html";

    if (!readFile(rootDir_ + rel, content)) return false;
    mime = guessMime(rel);
    return true;
}