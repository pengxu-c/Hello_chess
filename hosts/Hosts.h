// ============================================================
// hosts/Hosts.h - 宿主（运行方式）契约与三个内置实现
//
// 【宿主是什么】
//   宿主 = 「把内核、插件、界面拼起来跑起来」的那层代码。
//   同一个内核 + 同一批插件，可以有不同的跑法：
//     CliHost      —— 终端里人机对战
//     WebHost      —— 本地 HTTP 服务 + 浏览器界面
//     SelfPlayHost —— 无头自对弈/回归测试（连界面都没有，直接遍历内核）
//
//   宿主是「可执行文件的 main 选择哪一种玩法」，它自己不含任何棋规。
// ============================================================
#pragma once
#include "../app/Options.h"

#include <functional>
#include <string>

namespace gomoku {

// ---- 宿主契约：run 返回进程退出码 ----
class IAppHost {
public:
    virtual ~IAppHost() = default;
    virtual int run() = 0;
};

// 终端宿主：单界面（cli），适合调试、脚本、无图形环境
class CliHost final : public IAppHost {
public:
    explicit CliHost(Options opts) : opts_(std::move(opts)) {}
    int run() override;

private:
    Options opts_;
};

// 浏览器宿主：单界面（web），静态资源取自 webapp/
class WebHost final : public IAppHost {
public:
    explicit WebHost(Options opts) : opts_(std::move(opts)) {}
    int run() override;

    // 启动回调：端口绑定成功、服务已可用时调用一次。
    // 参数：实际访问地址、是否使用了自动分配的端口。
    // 存在的意义是让入口能在"双击启动、看不到控制台"的场景下给出可见提示
    // （地址只有在绑定之后才知道，所以不能在 run() 之前拿到）。
    using ReadyCallback = std::function<void(const std::string& url, bool ephemeralPort)>;
    void setReadyCallback(ReadyCallback cb) { ready_ = std::move(cb); }

private:
    Options opts_;
    ReadyCallback ready_;
};

// 无头自对弈宿主：连界面插件都不创建，直接驱动内核。
// 存在的意义是「内核可独立于任何界面被测试」—— 这本身就是微内核的红利。
class SelfPlayHost final : public IAppHost {
public:
    explicit SelfPlayHost(Options opts) : opts_(std::move(opts)) {}
    int run() override;

private:
    Options opts_;
};

// 自检宿主：在一个进程内把「内核 + 插件 + 界面 + HTTP 协议」整条链路跑一遍。
//
// 为什么需要它：本项目最常见的部署方式是"启动服务后由浏览器访问"，
// 而这类长驻进程在很多环境里不方便做端到端验证。自检把 HTTP 服务真的绑起来、
// 真的发一次请求、真的走一遍落子/悔棋/回放，然后自行退出 —— 于是
// CI 和开发者都能一条命令确认整条链路没坏。运行：Gomoku.exe --selftest
class SelfTestHost final : public IAppHost {
public:
    explicit SelfTestHost(Options opts) : opts_(std::move(opts)) {}
    int run() override;

    // 自检结束后继续托管网页界面的秒数（0 = 立即退出）。
    // 让"自动化断言"和"人工用浏览器验收"能共用一个入口。
    void setHoldSeconds(int s) { holdSeconds = s; }

private:
    Options opts_;
    int holdSeconds = 0;
};

}  // namespace gomoku
