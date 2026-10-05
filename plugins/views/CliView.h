// ============================================================
// plugins/views/CliView.h - 终端界面插件
//
// 【它的存在本身就是架构验证】
//   CliView 与 WebView 实现同一个 IView 接口、共用同一个内核，
//   但一个把状态画成文本、一个把状态推成 JSON/SSE。
//   任何"内核其实偷偷依赖了界面"的地方，都会在这两个界面之间暴露出来。
//
// 交互：轮到你输入时输入 `r c`（或 `r,c`）落子；输入 `h` 看命令表。
// 无头场景（自对弈 / CI）传 headless=true，界面不会等待任何输入。
// ============================================================
#pragma once
#include "../../contracts/IView.h"
#include <string>

namespace gomoku {

class CliView final : public IView {
public:
    explicit CliView(bool headless = false) : headless_(headless) {}

    std::string id() const override { return "cli"; }
    std::string displayName() const override { return "CLI (terminal)"; }

    void render(const ViewState& state) override;
    bool poll() override;
    void notify(const std::string& message) override;

private:
    void drawBoard(const ViewState& s) const;
    void printHelp() const;
    bool handleCommand(const std::string& line);

    bool headless_ = false;
    ViewState last_;              // 最近一次收到的快照
    long long drawnVersion_ = -1; // 已绘制过的版本（避免重复刷屏）
    bool prompted_ = false;       // 是否已就"请你落子"提示过
    bool notifiedEmpty_ = true;   // notify 是否为空（用于消音）
};

}  // namespace gomoku
