# 五子棋 Gomoku 

一个用 C++17 写的五子棋产品（可无损扩展至六子棋，有损扩展至多子棋）。内核只管棋规，**玩家、前端界面、存储、运行方式全部是插件**，
任何一项都能在不碰其它层的前提下替换或新增。

```text
双击 Gomoku.exe            → 浏览器界面
Gomoku.exe --view cli      → 终端界面
Gomoku.exe --selftest      → 45 项端到端自检
```

---

## 零、怎么运行、要带哪些文件

**最省事：整个 `build/Release/` 目录拷走即可，绿色无安装。**

```text
build/Release/
├─ Gomoku.exe          ← 主程序，双击就开网页界面
├─ GomokuBench.exe     ← 可选：棋力/正确性回归工具
├─ libcurl.dll         ← 必需（下面解释）
├─ z.dll               ← 必需（libcurl 的依赖）
└─ webapp/             ← 必需：网页界面本体
   ├─ index.html
   ├─ app.js
   └─ style.css
```

**运行依赖说明**

| 依赖 | 必需性 | 说明 |
|------|--------|------|
| `webapp/` | 网页界面必需 | 用 `--view cli`（终端下棋）时不需要 |
| `libcurl.dll` + `z.dll` | 需要 | 给"API AI"玩家用（远程大模型）。<br>但 `CURL::libcurl` 链接的是**导入库**，实现全在这两个 DLL 里，<br>缺了它双击 exe 会直接起不来 —— CMake 构建时已自动拷到 exe 旁边 |
| `config.json` | 可选 | 只有要用远程大模型玩家才需要。见 `config.example.json` |
| Visual C++ 运行库 | 通常不需要 | 编译是静态 CRT，换台没装 VS 的机器也能跑 |

**数据目录**：程序会在 exe 旁边建 `data/`（存棋谱、残局、统计）。这是运行时生成的，删掉不影响程序。

**三种启动方式**

```bash
# 1) 网页界面（默认）—— 双击 exe 等同于此
Gomoku.exe
Gomoku.exe --port 8099        # 指定固定端口；不加则自动分配

# 2) 终端界面
Gomoku.exe --view cli --black human --white tactical-max

# 3) 确认程序是好的（一条命令跑 45 项端到端检查）
Gomoku.exe --selftest
```

**怎么关闭**

| 运行方式 | 关闭办法 |
|----------|----------|
| 网页界面 / 双击 | **直接关掉那个黑色控制台窗口** |
| 终端界面 `--view cli` | 在窗口里输入 `q` + 回车，或按 Ctrl+C，或直接关窗口 |
| 自对弈 `--selfplay` | 跑完自动退出；中途可 Ctrl+C |

**控制台窗口存在期间程序就在运行** —— 这是有意为之：早先版本会隐藏控制台，
结果进程挂住了都没人察觉，攒下过几个杀不掉的僵尸进程。所以别关窗口，
关窗口就是退出。

---

## 一、微内核分层（依赖只能向下，绝不反向）

```text
contracts/   纯契约：接口 + 数据类型。零实现依赖，谁都能包含它
   ↑
kernel/      棋规内核：棋盘 / 规则 / 状态机 / 对局编排 / 存储边界
   ↑
app/         协议与装配：JSON 转换、参数解析、界面主循环驱动
   ↑
plugins/     插件：players（玩家）· views（界面）· storage（存储）
   ↑
hosts/       宿主：cli（终端）· web（HTTP）· selfplay（无头）· selftest（自检）
   ↑
main_microkernel.cpp   只做两件事：解析参数、挑一个宿主
```

| 层 | 目录 | 职责 | 换掉它意味着 |
|----|------|------|--------------|
| 契约 | `contracts/` | `IPlayer` / `IView` / `IControllable` / `IHost` / `PluginRegistry` | —— |
| 内核 | `kernel/` | `SquareBoard`(IBoard) · `GomokuRules`(IRules) · `MatchState` · `MatchSession` · `IStorageGateway` | 换棋盘实现、换玩法（禁手/六子棋） |
| 协议 | `app/` | `JsonProtocol` · `Options` · `ViewDriver` | 换前后端协议 |
| 插件 | `plugins/` | 玩家、界面、存储的具体实现 | 全部可替换、可增删 |
| 宿主 | `hosts/` | 启动方式（终端 / HTTP / 无头 / 自检） | 换运行形态 |

**内核不知道界面存在，界面拿不到棋盘对象。** 界面只能看 `ViewState` 只读快照，
只能通过 `IControllable` 投递命令 —— 这是"棋盘维护与前端泾渭分明"的落点。

---

## 二、加一个玩家（3 步，不动内核）

```cpp
// 1) plugins/players/MyPlayer.h/.cpp —— 只要实现 tick()
class MyPlayer final : public gomoku::IPlayer {
public:
    std::string id() const override { return "my-player"; }
    std::string displayName() const override { return "My Player"; }
    gomoku::Decision tick(const gomoku::ViewState& s, const gomoku::ThinkBudget&) override {
        // 从这里读局面（只读），返回一个坐标即可
        return gomoku::Decision::at(7, 7);
    }
};
```

```cpp
// 2) plugins/PlayerRegistry.cpp —— 加一行
reg.add(PlayerInfo{ "my-player", "My Player", "一句话说明", false, true, "" },
        [](const PlayerContext&) -> PlayerPtr { return std::make_unique<MyPlayer>(); });
```

```cmake
# 3) CMakeLists.txt 的 GOMOKU_CORE_SOURCES 里加上你的 .cpp
```

完成。这个玩家立刻出现在**网页下拉框、终端菜单、`--black/--white` 参数、自对弈**里，
因为所有入口都只读同一张注册表。

**想接一个已有的重型引擎？** 看 `plugins/players/EnginePlayers.cpp`：写十来行适配，
`TacticalMax` / `Minimax++` / API 大模型就是这么接进来的（引擎源码一行未改）。

**人类玩家不需要写 AI**：`human() == true` 时内核不调用 `tick()`，
而是把落子权交给界面，界面通过 `IControllable::play(r, c)` 送坐标回来。

---

## 三、加一个前端界面（3 步，不动内核、不动玩家）

```cpp
// 1) plugins/views/MyView.h/.cpp —— 只要实现 render() 与 poll()
class MyView final : public gomoku::IView {
public:
    std::string id() const override { return "my-view"; }
    std::string displayName() const override { return "My View"; }

    void render(const gomoku::ViewState& s) override { /* 画出来：终端/图片/协议/日志 */ }
    bool poll() override {
        // 处理自己的输入；需要操作棋局时用 session()->...（attach 时已注入）
        return true;   // 返回 false 表示请求退出程序
    }
};
```

```cpp
// 2) plugins/ViewRegistry.cpp —— 加一行
reg.add(ViewInfo{ "my-view", "My View", "一句话说明" },
        [](const ViewContext& ctx) -> std::unique_ptr<IView> {
            return std::make_unique<MyView>(ctx);
        });
```

```cmake
# 3) CMakeLists.txt 里加上你的 .cpp
```

完成：`Gomoku.exe --view my-view` 就能启动它。
主循环由 `app/ViewDriver.h` 统一驱动，宿主和自检跑的是同一条路径。

> 同一个进程可以挂多个界面（内核支持多观察者），例如"网页 + 日志文件"同时看同一局棋。

---

## 四、加一种存储 / 换一种存储

内核只认 `kernel/IStorageGateway.h`。默认实现 `plugins/storage/LegacyStorageGateway`
包装既有的文本存档（兼容旧 `data/*.txt`）。
想换 SQLite / 云存档：新写一个 `IStorageGateway` 实现，在宿主里换掉工厂函数即可，
内核、玩家、界面全都不用改。一个实现都不想写时用 `makeNullStorageGateway()`。

---

## 五、线程模型（重要）

- **控制线程**（`MatchSession`）：唯一能改棋盘的线程，所有命令串行执行。
- **调用方线程**（HTTP / 界面）：只做两件事 —— 投递命令、读只读快照。
- **AI 思考**：在工作线程里跑（`IPlayer::async()` 为真时）。思考期间内核不持锁，
  界面照样能刷新、悔棋、换人；若思考期间来了新命令，本次结果作废（请求代数检测）。
- **观察者回调**：在锁外调用，界面再慢也拖不住内核。

---

## 六、构建与运行

### 环境
CMake ≥ 3.15、Visual Studio 2022、vcpkg（`curl` + `nlohmann-json`）。
`CMakeLists.txt` 里的 vcpkg 路径按需修改：

```cmake
list(APPEND CMAKE_PREFIX_PATH "D:/SOFT/vcpkg/installed/x64-windows")
```

### 构建
```bat
cmake -B build -S .
cmake --build build --config Release
```
产物：`build\Release\Gomoku.exe`（微内核程序）、`GomokuBench.exe`（棋力基准）。
构建后 `webapp/` 会自动复制到 exe 旁边，双击即可用。

### 运行
```bat
Gomoku.exe                                            :: 浏览器界面（默认）
Gomoku.exe --view cli --black human --white tactical-max
Gomoku.exe --list                                     :: 列出全部玩家/界面插件
Gomoku.exe --selftest                                 :: 端到端自检（45 项）
Gomoku.exe --selfplay 2 --black tactical-max --white minimax --think 800
Gomoku.exe --port 8099 --no-browser --start --black human --white greedy
```

常用参数：`--view` `--black` `--white` `--board N` `--win N` `--port N` `--think ms`
`--data DIR` `--no-storage` `--headless` `--hold N`（自检后托管网页 N 秒供人工验收）。
旧编号写法 `--p1 7 --p2 5` 仍然可用（内部翻译成插件 id）。

### 浏览器界面

棋盘点击落子；侧栏可悔棋/中止/存档/载入残局/回放/翻转；
**"Swap player mid-game"可以直接把某一方换成任意其它玩家插件**；
按 `N` 新局、`U` 悔棋、`S` 存档、`R` 回放、`T` 主题、`Esc` 关弹窗。

前后端只通过三个接口对话：
```text
GET  /api/events   SSE：内核状态快照（只读，单向推送）
POST /api/cmd      { op:"new"|"move"|"undo"|"abort"|"swap"|"save"|"load"|"storage"|"quit", ... }
GET  /api/players | /api/games | /api/resumes | /api/stats
POST /api/replay
```

### 终端界面
轮到你时输入 `7 7`（或 `7,7`）落子；`u` 悔棋、`n` 新局、`s` 存档、`l <id>` 载入、
`a` 中止、`t` 切换存储、`b/w <id>` 换人、`players` 列插件、`h` 帮助、`q` 退出。

---

## 七、目录结构

```text
chess/
├── main_microkernel.cpp        # 微内核入口（→ Gomoku.exe）
├── contracts/                  # 契约层
│   ├── GameTypes.h             #   Stone/Coord/RulesConfig/ViewState/Move/PlayerInfo
│   ├── IPlayer.h               #   玩家插件接口（新增玩家只实现这个）
│   ├── IView.h                 #   界面插件接口（新增界面只实现这个）
│   ├── IControllable.h         #   界面操作内核的唯一通道
│   ├── IHost.h                 #   HTTP 宿主契约
│   ├── PluginRegistry.h        #   通用插件注册表模板
│   ├── PlayerRegistry.h        #   玩家注册表 + PlayerContext
│   └── ViewRegistry.h          #   界面注册表 + ViewContext
├── kernel/                     # 内核层
│   ├── IBoard.h / SquareBoard.*        # 棋盘（可换实现）
│   ├── GomokuRules.h/.cpp              # 规则（可换玩法）
│   ├── MatchState.h/.cpp               # 纯状态机：落子/判胜/悔棋重建
│   ├── IStorageGateway.h               # 存储边界
│   ├── NullStorageGateway.cpp          # 不记录的存储实现
│   └── MatchSession.h/.cpp             # 对局会话：编排 + 线程 + 观察者
├── app/                        # 协议与装配
│   ├── JsonProtocol.h/.cpp     # 内核状态 ↔ JSON 的唯一转换点
│   ├── Options.h/.cpp          # 命令行参数
│   └── ViewDriver.h            # 界面主循环驱动
├── plugins/                    # 插件层
│   ├── PlayerRegistry.cpp      # ★ 玩家插件唯一注册点
│   ├── ViewRegistry.cpp        # ★ 界面插件唯一注册点
│   ├── players/
│   │   ├── NativePlayers.h/.cpp    # 纯契约示例玩家（Random / Greedy）
│   │   └── EnginePlayers.h/.cpp    # 旧 AI 引擎适配器 + 引擎绑定表
│   ├── views/
│   │   ├── CliView.h/.cpp          # 终端界面
│   │   └── WebView.h/.cpp          # 浏览器界面（SSE + 命令队列）
│   └── storage/
│       └── LegacyStorageGateway.*  # 包装既有文本存档
├── hosts/                      # 宿主层
│   ├── Hosts.h
│   ├── CliHost.cpp             # 终端宿主
│   ├── WebHost.cpp             # 浏览器宿主
│   ├── SelfPlayHost.cpp        # 无头自对弈（不含任何界面）
│   ├── SelfTestHost.cpp        # 45 项端到端自检
│   └── web/
│       ├── HttpServer.h/.cpp        # 唯一包含 httplib 的地方
│       └── BrowserLauncher.h/.cpp
├── webapp/                     # 前端资源（改它不需要重编译 C++）
│   ├── index.html  app.js  style.css
├── engine/                     # 既有 AI 引擎（源码未改，被适配器包装成插件）
│   ├── core.* player.* threat.* tactical_max.*   # 棋盘/裁判 + 各档 AI
│   ├── ai_config.* ai_player.*                   # 远程大模型配置与请求
│   └── storage.*                                 # 旧文本存档实现
├── bench/                      # 棋力基准（复用同一套玩家插件，不链接网络栈）
└── third_party/httplib.h       # 本地 HTTP 服务单头文件
```

> 旧版单文件程序（`main.cpp` / `controller.*` / `ui.*` / `match.*` /
> `player_registry.*` / `server/`）已删除：微内核版本覆盖了它的全部功能。

---

## 八、从旧架构迁移得到了什么

| 旧架构的问题 | 现在的做法 |
|--------------|-----------|
| `player_registry.cpp` 是"唯一注册点"，但只有编号、没有界面/存储注册点 | 玩家、界面各有一张注册表，存储是接口；三类扩展点结构一致 |
| 人类输入与 AI 思考都挤在 `Player::place()` 轮询里 | 人类 = `human()==true` 走界面命令；AI = `tick()` 在工作线程 |
| 界面拿得到棋盘、能直接改棋局 | 界面只有 `ViewState` 只读快照 + `IControllable` 命令 |
| 存储接口依赖具体 `Board&`（`undoMoves(Board&)`） | 存储只丢记录，棋盘由内核按自己的实现重建 |
| 新增界面要改 `main.cpp` 的 `if (mode == ...)` | `--view <id>` + 注册表一行 |
| 长耗时 AI/远程 API 会卡住界面 | 内核在工作线程调用 `tick()`，思考期间不持锁 |
| 终局后主循环忙等，导致新命令迟迟不生效 | `hasWorkLocked()` 只在"进行中"返回真，其余情况睡在条件变量上 |

---

## 九、自检

```bat
Gomoku.exe --selftest
```
在一个进程内跑完 45 项断言，覆盖：插件注册表 → 内核（开新局/落子/判胜/悔棋/换人/中止）
→ 存储插件 → 界面插件观察者 → HTTP 宿主（真绑端口、真发请求：静态资源、状态查询、
命令总线、目录查询、404）。任何一层边界被改坏都会在这里变红。

自检期间发现并修掉的真实缺陷（都写在了对应源码注释里）：
1. `MatchSession` 成员声明顺序错误 → `storageDefault_` 未初始化即被读取，悔棋静默失效。
2. `hasWorkLocked()` 在终局后仍返回真 → 主循环忙等，`startGame` 命令进队列却不被处理。
3. `CliView` 先取第一个词当命令 → 输入 `7 7` 被当成未知命令 `7`。
4. 管道输入的 `\r` 未裁剪 → 坐标解析失败。
5. Windows 子系统下 `--list/--selfplay` 输出为空且崩溃 → 改用控制台子系统 + 主动隐藏窗口。

---

## 十、常见问题

**Q：双击 Gomoku.exe 没反应？**
A：它会启动本地服务并打开浏览器。若浏览器没开，手动访问
`http://127.0.0.1:<端口>/`；端口会打印在控制台（Web 模式下控制台窗口被隐藏，
端口也可从 `--port 8099` 固定）。

**Q：提示找不到 webapp？**
A：`webapp/` 必须和 exe 同级（构建时已自动复制）。手动跑时可在 `chess/` 目录下运行，
或把 `webapp/` 拷到 exe 旁边。

**Q：API AI（远程大模型）怎么配？**
A：在 `chess/` 下建 `config.json`（格式见 `config.example.json`）。未配置时该档位在界面上
标为 not configured，选择它开局会自动回退到 `Minimax++`。

**Q：`Hello_chess.exe` 是什么？**
A：旧版单文件程序，保留一个版本周期用于对照行为（棋力、存档格式）。确认无回归后可删掉
`main.cpp`、`controller.*`、`ui.*`、`iui.h`、`match.*`、`player_registry.*`、`server/`
以及 CMakeLists 里的 `Hello_chess` 目标。

---

## 十一、依赖与致谢

- **cpp-httplib** — 本地 HTTP 服务（只被 `hosts/web/HttpServer.cpp` 包含）
- **libcurl** — API 玩家网络请求
- **nlohmann/json** — 配置与 JSON 协议

## 致谢 / 许可

见 `LICENSE`。
