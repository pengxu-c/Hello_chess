# Hello_chess

五子棋（可参数化连珠数）：**C++17 + 本地 Web UI**，CMake（MSVC）构建。
棋盘在浏览器中渲染，后端是进程内的本地 HTTP 服务；另保留纯文本终端模式。

> 早期版本基于 EasyX 图形库，现已**完全移除**——界面改为「本地 HTTP 服务 + HTML/JS 前端」，
> 并保留一个不依赖任何图形库的文本终端界面（`--cli`）。

---

## 一、如何复刻（主线）

这一节的目标是：**照着做，就能从零把这个项目重建出来。**

### 1. 环境准备

| 项目 | 要求 |
|:-----|:-----|
| 操作系统 | Windows 10 / 11（64 位） |
| 编译器 | MSVC（Visual Studio 2019 / 2022，勾选"使用 C++ 的桌面开发"） |
| 构建工具 | CMake ≥ 3.15 |
| 包管理 | vcpkg |
| 第三方库 | curl、nlohmann-json（vcpkg 安装）；cpp-httplib（单头文件，手工放置） |

> 不再需要 EasyX，也不需要任何图形库 SDK。

### 2. 获取依赖

用 vcpkg 安装网络与 JSON 库：

```bash
vcpkg install curl:x64-windows nlohmann-json:x64-windows
```

cpp-httplib 是**单头文件**，不经过 vcpkg，直接下载放进 `third_party/`：

```bash
mkdir third_party
curl -L -o third_party/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
```

最后按你机器的实际位置修改 `CMakeLists.txt` 中的 vcpkg 前缀：

```cmake
list(APPEND CMAKE_PREFIX_PATH "D:/SOFT/vcpkg/installed/x64-windows")
```

### 3. 目录结构

复刻时按下表建立文件；`data/` 由程序运行时自动创建，无需手工建。

```
chess/
├── main.cpp                       入口：默认 Web 模式，--cli 走终端
├── core.h / core.cpp              Board / Judge / Stats / Pos / ChessType / scanLine
├── ui.h / ui.cpp                  文本终端界面（渲染棋盘 + 读坐标）
├── player.h / player.cpp          Player 基类 + HumanPlayer + 各类 AI
├── threat.h / threat.cpp          ThreatDetector（必胜 / 必防威胁检测）
├── ai_config.h / ai_config.cpp    AIConfig（API AI 的 JSON 配置加载）
├── ai_player.h / ai_player.cpp    APIPlayer（远程大模型棋手）
├── storage.h / storage.cpp        StorageManager（悔棋 / 回放 / 残局 / 统计）
├── controller.h / controller.cpp  GameController（终端对局流程）
├── experimental_tactical_player.h / .cpp   实验棋手
├── server/                        本地 HTTP 服务层
│   ├── session.h/.cpp              对局状态机（线程安全，AI 锁外思考）
│   ├── game_server.h/.cpp          REST 路由 + SSE 推送 + 端口分配
│   ├── asset_provider.h/.cpp       前端资源读取（磁盘 web/，接口可扩展内嵌）
│   └── browser_launcher.h/.cpp     打开浏览器（默认系统浏览器，预留 WebView2）
├── web/                           前端（HTML / CSS / JS）
│   ├── index.html
│   ├── style.css
│   └── app.js
├── third_party/httplib.h          cpp-httplib 单头文件
├── docs/web-ui-design.md          Web UI 设计方案（模块划分 / 接口 / 风险）
├── config.example.json            API AI 配置模板
└── CMakeLists.txt
```

### 4. 构建

```bash
cmake -B build -S .
cmake --build build --config Release
```

产物为 `build/Release/Hello_chess.exe`。

程序编译为 **Windows 子系统**（`WIN32_EXECUTABLE` + `wWinMain` 入口），
因此双击运行时**不会出现控制台黑框**。

### 5. 运行与验证

| 方式 | 命令 | 说明 |
|:-----|:-----|:-----|
| 浏览器界面 | 双击 `Hello_chess.exe` | 起本地服务并自动打开默认浏览器 |
| 固定端口 | `Hello_chess.exe --port 8799` | 便于调试与脚本 |
| 不开浏览器 | `Hello_chess.exe --no-browser` | 手动访问启动时打印的地址 |
| 文本终端 | `Hello_chess.exe --cli` | 纯文本棋盘，输入 `行 列`（0 起）落子 |

> **重要：启动后必须点击 `New Game`，选好棋盘尺寸、连珠数与黑白棋手，再点 `Start`，对局才会开始。**
> 页面刚打开时处于 `Idle` 状态，棋盘是空的，点击棋盘不会有反应——这是正常的，不是故障。

完整使用流程：

1. 双击 exe → 浏览器自动打开页面（右侧状态显示 `Idle`）。
2. 点顶部 **New Game** → 设置棋盘尺寸 / 连珠数 / 黑方棋手 / 白方棋手（默认黑 Human、白 Minimax++）→ 点 **Start**。
3. 轮到人类时，直接在棋盘上点击空位落子；AI 回合侧栏会显示 `AI thinking...`。
4. 侧栏可 **Undo**（悔棋）、**Save**（存残局）、**Resumes**（载入残局）、**Replays**（回放）、**Stats**（统计）。
5. 顶部 **Quit** 关闭服务；也可以直接关掉进程。

验证后端链路是否打通：

```bash
curl http://127.0.0.1:<port>/api/state     # 返回棋盘状态 JSON
curl -N http://127.0.0.1:<port>/api/events # SSE 状态推送
```

### 6. 复刻关键点（最容易踩的坑）

1. **启动后要点 New Game**：后端初始状态是 `Idle`，前端只在 `status == "InProgress"` 且轮到人类时才接受点击。
2. **无控制台黑框**：必须设 `WIN32_EXECUTABLE`（链接 `/SUBSYSTEM:WINDOWS`）并提供 `wWinMain` 入口；
   只用 `ShowWindow(SW_HIDE)` 隐藏是不够的——窗口对象依然存在。`--cli` 模式则用 `AttachConsole/AllocConsole`
   + `freopen("CONOUT$")` 按需附加控制台。
3. **前端资源定位**：`main.cpp` 的 `resolveWebRoot()` 依次探测
   `web` → `exe/web` → `exe/../../web` → `exe/../web`，取第一个含 `index.html` 的目录。
   分发时 **`web/` 文件夹必须与 exe 放在一起**（或放在 exe 上两级）。
4. **必须链接 `ws2_32`**：cpp-httplib 在 Windows 下依赖 Winsock，漏掉会链接失败。
5. **端口自动分配 + 独立监听线程**：用 `bind_to_any_port("127.0.0.1")` 拿空闲端口，
   再用 `listen_after_bind()` 放到独立线程跑，主线程才不会被阻塞。
6. **状态推送用 SSE 而非轮询**：`/api/events` 用 `set_chunked_content_provider` 保持长连接，
   仅在状态版本号变化时推送，静止时零流量、零 CPU 唤醒。
7. **并发只在 `SessionController` 一层**：单一线程（loop）独占 `Board` 与玩家对象、串行推进回合；
   AI 思考在**锁外**执行，HTTP 线程只加锁读写「共享快照」并投递请求。核心逻辑（`Board/Judge/Player`）不加锁。
8. **核心逻辑与交互层彻底解耦**：`Board / Judge / Player / ThreatDetector / StorageManager`
   不依赖任何界面代码，换界面（浏览器 ↔ 文本 ↔ 未来 WebView2）无需改核心。
9. **文本 UI 的接口约定**：`UI` 类保持与原图形版一致的接口签名
   （`initWindow / render / pollMouse / messageBox / askYesNo / pollKey / setLayout`），
   因此 `controller.cpp`、`player.cpp` 一行都不用改，就能从图形界面切换到文本界面。

---

## 二、功能与配置（次要）

### 棋手类型

| 编号 | 棋手 | 说明 |
|:----:|:-----|:-----|
| 1 | Human | 人类玩家（浏览器点击 / 终端输入坐标） |
| 2 | EasyJudge | 随机 + 防输（最弱陪练） |
| 3 | PureGreed 1.0 | 纯防守评分 |
| 4 | PureGreed 1.1 | 攻防评分 |
| 5 | Minimax++ | αβ 搜索（最强） |
| 6 | API AI | 远程大模型，需配置，未配置自动回退 5 |
| 7 | Tactical++ | 实验棋手 |

> 难度名称仅作相对区分，不代表实际棋力。

### API AI 配置（玩家 6）

按 `config.example.json` 补填 `config.json` 中的 `api_url`、`api_key`、`model` 三个必填字段即可
（`display_name` 留空则自动显示模型名）。`config.json` 含密钥，已被 `.gitignore` 忽略。
未配置或调用出错时自动回退玩家 5，不影响程序运行。

### 记忆存储

在 New Game 对话框勾选 **Enable memory storage** 后启用，数据写入 `data/`
（`games/`、`resumes/`、`stats.txt`），采用 `key=value` 文本格式，向后兼容。
支持悔棋、存残局、残局续弈、棋局回放、跨局统计。

### HTTP 接口一览

| 方法 | 路径 | 说明 |
|:-----|:-----|:-----|
| GET | `/api/state` | 当前状态（首次拉取 / SSE 兜底） |
| GET | `/api/events` | SSE 状态推送 |
| GET | `/api/players` | 棋手目录 |
| POST | `/api/newgame` | 开新局 `{boardSize,winLength,p1Type,p2Type,storageEnabled}` |
| POST | `/api/move` | 人类落子 `{r,c}` |
| POST | `/api/undo` | 悔棋 `{n}` |
| POST | `/api/save` | 存残局 `{note}` |
| GET | `/api/resumes` | 残局列表 |
| POST | `/api/load` | 载入残局 `{id}` |
| GET | `/api/games` | 棋局列表 |
| POST | `/api/replay` | 回放定位 `{id,step}` |
| GET | `/api/stats` | 全局统计 |
| POST | `/api/quit` | 退出服务 |

---

## 三、技术细节（次要）

### 分层

| 层 | 组成 | 职责 |
|:---|:-----|:-----|
| 核心逻辑层 | `Board / Judge / Stats / scanLine / ThreatDetector / Player 及派生` | 与界面完全无关 |
| 交互层 | `ui.h/.cpp`（文本）、`web/` + `server/`（浏览器） | 渲染与输入 |
| 调度层 | `controller.cpp`（终端）、`session.cpp` + `game_server.cpp`（Web） | 回合与请求调度 |
| 持久化层 | `StorageManager` | 棋局 / 残局 / 统计 |

### Web UI 架构

```
浏览器 (web/)  ──HTTP/SSE──▶  GameServer (cpp-httplib)  ──▶  SessionController  ──▶  核心逻辑层
```

前端以 Canvas 绘制棋盘，最后一手用彩色圆环标记；状态经 SSE 推送，无轮询。
完整设计见 [`docs/web-ui-design.md`](docs/web-ui-design.md)。

### 文本终端界面

`ui.cpp` 用字符画棋盘（`.` 空位、`X` 黑子、`O` 白子，小写标记最后一手），
人类落子输入 `行 列`；仅在棋盘变化时重绘，避免闪烁。

---

## 当前开发状态

- [x] 文本终端对局（`--cli`）
- [x] 本地 HTTP 服务 + 静态资源 + SSE 状态通道
- [x] 浏览器渲染棋盘（Canvas + 最后一手色环）
- [x] 移除 EasyX 依赖
- [x] 浏览器内落子对局（`SessionController` + `/api/newgame` + `/api/move`）
- [x] AI 在工作线程思考（不阻塞状态推送）
- [x] 存储 / 回放 / 统计接入界面
- [x] 无控制台窗口（Windows 子系统）
- [x] 浅色明亮主题

---

## 致谢

- **cpp-httplib**：本地 HTTP 服务：<https://github.com/yhirose/cpp-httplib>
- **libcurl**：API AI 网络请求：<https://curl.se>
- **nlohmann/json**：配置解析与 JSON 处理：<https://github.com/nlohmann/json>
