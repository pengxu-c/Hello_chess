// ============================================================
// asset_provider.h - 前端静态资源提供者
// 抽象接口：按 URL 路径返回资源内容与 MIME 类型。
// 默认实现 DiskAssetProvider 从磁盘 web/ 目录读取；
// 将来若改为内嵌资源，只需新增一个实现，业务代码不动。
// ============================================================
#pragma once
#include <string>

class AssetProvider {
public:
    virtual ~AssetProvider() = default;
    // 按 URL 路径（如 "/index.html"）取资源。
    // 成功返回 true 并填充 content 与 mime；失败返回 false。
    virtual bool get(const std::string& urlPath,
                     std::string& content,
                     std::string& mime) const = 0;

    // 启动自检：资源是否就绪（磁盘实现检查 index.html 是否存在）
    virtual bool ready() const = 0;
};

// ---- 磁盘实现：从指定根目录读取文件 ----
class DiskAssetProvider : public AssetProvider {
public:
    explicit DiskAssetProvider(std::string rootDir);

    bool get(const std::string& urlPath,
             std::string& content,
             std::string& mime) const override;

    // 启动自检：根目录下 index.html 是否存在
    bool ready() const override;

private:
    std::string rootDir_;                                    // web 资源根目录（不带尾斜杠）
    static std::string guessMime(const std::string& path);   // 按扩展名推断 MIME
    static bool readFile(const std::string& path, std::string& out);  // 读取整个文件
    static bool isPathSafe(const std::string& urlPath);      // 拒绝目录穿越
};