# 方案分析卡：EasyX 桌面界面 → 本地 Web UI

> 目标：删除 EasyX，改为「C++ 本地 HTTP 服务 + HTML/JS 前端」，用系统默认浏览器打开。
> 同时彻底移除控制台交互（无黑框、无 `printf`/`scanf`），保留 `--cli` 命令行入口用于脚本与回归测试。

---

## 一、目标与约束

| 项 | 内容 |
|---|---|
| 目标 | 无控制台窗口；所有交互在浏览器界面内完成 |
| 技术约束 | C++17 + CMake；复用已有 `curl`、`nlohmann/json`；不引入重型框架 |
| 代码约定 | 注释用简体中文，界面可见文字用英文（沿用 CLAUDE.md） |
| 最高优先级 | 可扩展性：前端与后端解耦，将来可无痛替换外壳（浏览器 → WebView2） |
| 保留能力 | `Hello_chess.exe --cli` 走原终端流程，供脚本/调试/回归对照 |

---

## 二、总体架构

```
┌──────────────────────── 浏览器 (web/) ────────────────────────┐
│  index.html   app.js   style.css                               │
│  Canvas 棋盘 / 菜单 / 棋手选择 / 存储面板 / 回放控制 / 统计     │
└──────────▲────────────────────────────────┬───────────────────┘
           │ fetch JSON (REST)              │ SSE GET /api/events
           │                                ▼
┌──────────┴────────────────────────────────────────────────────┐
│                 C++ 进程 Hello_chess.exe                        │
│  ┌──────────────────┐      ┌──────────────────────────────┐    │
│  │ GameServer       │      │ 原核心（保持不动）            │    │
│  │ cpp-httplib 路由 │─────▶│ Board / Judge / Player        │    │
│  │ JSON 序列化      │      │ ThreatDetector / AIConfig     │    │
│  └──────────────────┘      │ StorageManager                │    │
│  ┌──────────────────┐      └──────────────────────────────┘    │
│  │ SessionController│      ┌──────────────────────────────┐    │
│  │ 对局状态机+互斥锁 │      │ CLI 模式 → 原 GameController  │    │
│  └──────────────────┘      └──────────────────────────────┘    │
└────────────────────────────────────────────────────────────────┘
```

设计要点：**核心逻辑层零改动**。`Board / Judge / Player / ThreatDetector / StorageManager / AIConfig` 原样复用，
改造只发生在「交互层」（删除 EasyX）与「调度层」（阻塞循环 → 事件驱动状态机）。

---

## 三、模块划分

### 新增

| 文件 | 职责 |
|---|---|
| `third_party/httplib.h` | cpp-httplib 单头文件 HTTP 服务端，直接放仓库，不走 vcpkg |
| `server/game_server.h/.cpp` | HTTP 服务封装：路由注册、随机端口、启动/停止、SSE 推送 |
| `server/browser_launcher.h/.cpp` | 浏览器启动抽象：默认 `ShellExecuteW`，预留 WebView2 实现 |
| `server/session.h/.cpp` | `SessionController`：对局状态机（新局/落子/悔棋/AI 思考/结束），线程安全 |
| `server/json_view.h/.cpp` | 领域对象 → JSON：`Board`、`GameRecord`、`GlobalStats`、棋手列表 |
| `server/asset_provider.h/.cpp` | 前端资源读取抽象：默认读磁盘 `web/` 目录，接口可扩展为内嵌 |
| `web/index.html` | 页面骨架 + 各面板容器 |
| `web/style.css` | 样式（棋盘配色、色环标记、响应式布局、美观字体） |
| `web/app.js` | 前端逻辑：Canvas 绘制、fetch 调用、SSE 订阅、事件绑定 |

### 改造

| 文件 | 改动 |
|---|---|
| `main.cpp` | 解析 `--cli`；默认 Web 模式；删除结尾 `getchar()` 暂停 |
| `controller.h/.cpp` | `configureRules()`/`selectPlayers()` 标注为 CLI 专用；对局核心抽给 `SessionController` 复用 |
| `player.h/.cpp` | `HumanPlayer` 不再依赖 `UI&`，改为从 `Session` 的待落子队列取输入 |
| `storage.h/.cpp` | `handleConsoleCommand` 保留给 CLI；REST 直接调用已有的 `listGames/saveResume/globalStats` 等 |
| `CMakeLists.txt` | 删除 EasyX 查找与链接；加 `WIN32_EXECUTABLE`；加 httplib 头文件目录 |
| `README.md` | 更新运行方式、配置说明、架构图（对应偏好：README 状态同步） |

### 删除

- `ui.h` / `ui.cpp`（EasyX 封装整体删除）
- CMake 中 EasyX 的 `find_path`、`target_include_directories`、`target_link_directories`，以及 `gdi32/msimg32` 等仅服务 EasyX 的链接项
- `cmake-local.cmake` 中的 EasyX 路径（文件本身可留空）

---

## 四、REST 接口设计

| 方法 | 路径 | 请求体 | 响应 | 说明 |
|---|---|---|---|---|
| GET | `/api/state` | — | 状态对象 | 首次拉取与调试（SSE 的兜底） |
| GET | `/api/events` | — | SSE 事件流 | 状态变化时推送，静止时零流量 |
| POST | `/api/newgame` | `{boardSize,winLength,p1Type,p2Type,storageEnabled}` | `{ok,state}` | 开新局 |
| GET | `/api/players` | — | `[{id,name,isHuman,apiReady}]` | 棋手列表 + API 配置状态 |
| POST | `/api/move` | `{r,c}` | `{ok,state}` | 人类落子 |
| POST | `/api/undo` | `{n}` | `{ok,state}` | 悔棋 |
| POST | `/api/save` | `{note}` | `{ok,id}` | 存残局 |
| GET | `/api/resumes` | — | `[{id,meta}]` | 残局列表 |
| POST | `/api/load` | `{id}` | `{ok,state}` | 载入残局 |
| GET | `/api/games` | — | `[{id,meta}]` | 棋局列表 |
| POST | `/api/replay` | `{id,step}` | `{ok,state}` | 回放定位到第 step 步 |
| GET | `/api/stats` | — | `GlobalStats` | 全局统计 |
| POST | `/api/quit` | — | `{ok}` | 请求退出进程 |

`GET /api/state` 响应示例：

```json
{
  "boardSize": 15,
  "winLength": 5,
  "cells": [0, 0, 1, 0, -1, 0, 0],
  "turn": 1,
  "lastBlack": [7, 7],
  "lastWhite": [7, 8],
  "status": "InProgress",
  "canUndo": true,
  "moveCount": 12,
  "players": { "black": "Human", "white": "Minimax++" },
  "thinking": false
}
```

> `cells` 为一维数组，索引 `r * boardSize + c`，取值 `0=空 / 1=黑 / -1=白`，与 `Board::map_` 一致，序列化零转换成本。

---

## 五、并发模型（本方案最大风险点）

原 `playOneGame()` 是**单线程阻塞循环**（`controller.cpp:271-344`）：轮询鼠标 → 落子 → 渲染 → 再轮询。
改为 HTTP 后，请求由 httplib 的线程池并发处理，会同时访问 `Board`，必须处理数据竞争。

设计方案：

1. `SessionController` 内部持有全部游戏状态 + 一把 `std::mutex`；所有读写经加锁访问。
2. AI 思考放在**独立工作线程**：轮到 AI 时置 `thinking=true` 并异步计算，算完写入待落子并置回 `false`。
3. 人类落子走 `POST /api/move`，写入"待落子"槽位，由状态机消费。
4. 状态推送采用 SSE：浏览器建立 `GET /api/events` 长连接，服务端仅在状态变化时推一帧 JSON；静止时零流量、零唤醒。`/api/state` 仍保留，供首次拉取与调试。

> 关键：`Board/Judge/Player` 内部**不加锁**，锁只在 `SessionController` 一层，避免污染核心逻辑。

---

## 六、前端界面设计（替代原控制台流程）

| 原控制台交互 | 前端替代 |
|---|---|
| 主菜单 New/Load/Replay/Stats（`controller.cpp:347-400`） | 顶部导航 + 各面板 |
| 规则配置 `fgets/sscanf_s`（`controller.cpp:69-108`） | New Game 面板：棋盘尺寸、连珠数输入 + 校验 |
| 棋手选择 1-7（`controller.cpp:111-146`） | 双方下拉框，API 未配置的档位显示徽标并可自动回退 5 |
| 控制台命令 `h/ls/s/u/a/st`（`storage.cpp:328-349`） | 侧栏按钮：Undo / Save / Abort + 列表弹窗 |
| 键盘回放（`controller.cpp:235-265`） | 回放面板：上一步 / 下一步 / 播放 / 进度滑块 |
| 胜负弹窗 + "Play again?" | 结果浮层 + New Game 按钮 |

视觉要点：
- 棋盘与棋子用 Canvas 绘制，沿用"黑底白线/白棋白填黑线"的现有风格。
- **最后一手用彩色圆环标记**（黑手环 / 白手环），而非闪烁边框。
- 字体选用系统无衬线 + 中文回退，保证排版美观。
- 深浅色适配、悬停高亮与落子动画作为增强项。

---

## 七、启动与分发

启动流程：

```
main()
 ├─ 含 --cli ? → 原 GameController::run()（终端模式）
 └─ 默认：选空闲端口(127.0.0.1) → GameServer.listen()
          → ShellExecuteW 打开默认浏览器
          → 主线程等待（/api/quit 或超时）
```

前端资源分发（已决策）：**始终使用磁盘 `web/` 目录**。

- C++ 服务收到 `GET /` 时，从 exe 同级的 `web/` 目录读取并返回。
- 分发时需将 `web/` 文件夹与 exe 一并提供；启动时若检测不到 `web/index.html`，在日志中给出明确错误提示。
- 读取逻辑以 `AssetProvider` 接口封装，将来若要改为内嵌只需替换实现，业务代码不动。

退出策略：提供界面内 Quit 按钮（`/api/quit`）；并可加"长时间无请求自动退出"兜底。

---

## 八、改造步骤（分阶段，每阶段可独立验证）

| 阶段 | 内容 | 验证标准 |
|---|---|---|
| P0 骨架 | 引入 httplib，`/api/state` 返回假数据，前端显示空棋盘 | 浏览器能打开、棋盘渲染出来 |
| P1 对局核心 | `SessionController` + `/api/newgame` + `/api/move` + 真实 `/api/state` | 人类双方能鼠标落子并判胜负 |
| P2 AI 接入 | AI 工作线程 + `thinking` 状态 + `/api/players` | 人机对局正常，界面有思考提示 |
| P3 存储/回放/统计 | `/api/save|load|games|replay|stats` + 对应面板 | 存档/读档/回放/统计全部可用 |
| P4 收尾 | 删除 EasyX、加 `WIN32_EXECUTABLE`、`--cli` 保留 | 双击 exe 无黑框，`--cli` 仍可跑 |
| P5 回归 | 编译 + 对局回归 + README 更新 | `cmake --build build --config Release` 通过 |

---

## 九、风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| 阻塞循环改事件驱动 | 对局逻辑重构易引入 bug | 核心层零改动，只重构调度层；用 `--cli` 旧路径做对照回归 |
| 多线程访问 `Board` | 数据竞争、随机崩溃 | 锁只在 `SessionController` 一层；AI 线程仅产出待落子 |
| 默认浏览器打开 | 切标签、无桌面应用感 | 抽象 `BrowserLauncher` 接口，将来换 WebView2 只替换实现 |
| 端口占用 / 防火墙拦截 | 服务启动失败 | 随机端口 + 占用探测 + 失败重试 |
| 前端资源分发 | 使用者需连 `web/` 文件夹一起拿 | 启动时校验 `web/index.html` 并给出明确日志；`AssetProvider` 接口可切内嵌 |
| 无控制台后无日志 | 排错困难 | 日志写入 `data/logs/`，保留错误栈 |
| 外部 IDE 还原文件 | 改动丢失 | 每次改完 `grep` 确认实际内容（沿用 CLAUDE.md 提醒） |

---

## 十、已确认决策

| 决策点 | 结论 |
|---|---|
| 前端资源分发 | 始终磁盘 `web/` 目录，exe 与 `web/` 文件夹一起分发 |
| 状态刷新 | SSE 推送（`GET /api/events`），仅在状态变化时推送 |
| 界面承载 | 系统默认浏览器打开 |
| WebView2 | 现在就预留 `BrowserLauncher` 接口，默认实现调系统浏览器 |
| EasyX | 删除 `ui.h`/`ui.cpp`，不保留可切换编译 |
| 命令行 | 保留 `--cli` 入口，用于脚本 / 调试 / 回归对照 |