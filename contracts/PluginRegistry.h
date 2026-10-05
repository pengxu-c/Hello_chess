// ============================================================
// contracts/PluginRegistry.h - 通用插件注册表（玩家 / 界面 / 未来任何扩展点共用）
//
// 一个注册表 = 「元信息 + 工厂函数」列表 + 按 id 查找/创建。
// 玩家与界面共用这份模板实现，因此「新增一个扩展点」和「新增一个插件」
// 成本几乎一样低。
//
// 注册在启动时完成（plugins/*Registry.cpp 的 registerBuiltin* 函数），
// 不做运行期动态库扫描：零魔法、可断点、可静态分析。
// 将来要热插拔 DLL，只需另写一个「从动态库批量 add()」的函数，本文件不动。
// ============================================================
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gomoku {

// TProduct : 插件产品类型，如 IPlayer
// TInfo    : 元信息类型，必须含字段 id / display / description，如 PlayerInfo
// TContext : 创建上下文，如 PlayerContext
template <typename TProduct, typename TInfo, typename TContext>
class PluginRegistry {
public:
    using Factory = std::function<std::unique_ptr<TProduct>(const TContext&)>;

    // 注册一个插件。同 id 重复注册以最后一次为准（便于上层覆盖内置实现）。
    void add(const TInfo& info, Factory factory) {
        for (auto& e : items_) {
            if (e.info.id == info.id) {
                e.info = info;
                e.factory = std::move(factory);
                return;
            }
        }
        items_.push_back(Entry{ info, std::move(factory) });
    }

    // 按 id 创建；未知 id 或工厂返回 nullptr 时得到 nullptr。
    std::unique_ptr<TProduct> create(const std::string& id, const TContext& ctx) const {
        const Factory* f = factoryOf(id);
        if (!f || !(*f)) return nullptr;
        return (*f)(ctx);
    }

    bool contains(const std::string& id) const {
        return factoryOf(id) != nullptr;
    }

    std::string displayOf(const std::string& id) const {
        if (const TInfo* i = find(id)) return i->display;
        return "Unknown";
    }

    const TInfo* find(const std::string& id) const {
        for (const auto& e : items_)
            if (e.info.id == id) return &e.info;
        return nullptr;
    }

    // 全部元信息（保持注册顺序，界面可据此稳定排序）。
    // 按值返回：注册表极小、调用极少，换来的是线程安全与无隐藏状态。
    std::vector<TInfo> catalog() const {
        std::vector<TInfo> out;
        out.reserve(items_.size());
        for (const auto& e : items_) out.push_back(e.info);
        return out;
    }

    std::vector<std::string> ids() const {
        std::vector<std::string> v;
        v.reserve(items_.size());
        for (const auto& e : items_) v.push_back(e.info.id);
        return v;
    }

    size_t size() const { return items_.size(); }

private:
    struct Entry {
        TInfo info;
        Factory factory;
    };

    const Factory* factoryOf(const std::string& id) const {
        for (const auto& e : items_)
            if (e.info.id == id) return &e.factory;
        return nullptr;
    }

    std::vector<Entry> items_;
};

}  // namespace gomoku
