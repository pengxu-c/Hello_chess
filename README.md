# 五子棋 Gomoku

探索更加强大的力量来解决目前的问题（请使用上一版本来进行体验）！！！！

一个 C++17 写的五子棋程序（可以无损扩展至六子棋、有损扩展至更多子棋）
支持人类对战和多种 AI 档位。默认启动本地 HTTP 服务，用浏览器打开图形界面。

---

## 一、运行程序（5 分钟搞定）

### 1. 环境准备

你的电脑需要先装好这些：

| 软件 | 最低版本 | 说明 |
|------|---------|------|
| CMake | 3.15 | 构建工具 |
| Visual Studio 2022（或 Build Tools） | 任意 | 提供 MSVC 编译器 |
| Git | 任意 | 拉代码用 |

**检查是否装好**：打开"开始菜单"搜索 `Developer Command Prompt for VS 2022`，输入 `cmake --version` 能看到版本号就 OK。

### 2. 下载代码

```
git clone https://github.com/你的账号/2026.8chess.git
cd 2026.8chess\chess
```

### 3. 安装依赖（vcpkg）

这个项目用 `vcpkg` 管理第三方库。如果你还没有 vcpkg：

```
# 在任意位置克隆 vcpkg（比如 D 盘）
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.ootstrap-vcpkg.bat
```

然后告诉 CMake vcpkg 在哪（编辑 `chess/CMakeLists.txt` 第 45 行，改成你的路径）：

```cmake
list(APPEND CMAKE_PREFIX_PATH "D:/SOFT/vcpkg/installed/x64-windows")
```

### 4. 一键构建

在 `Developer Command Prompt` 里，进入 `chess` 目录：

```
cmake -B build -S .
cmake --build build --config Release
```

构建成功后，`build\Release\Hello_chess.exe` 就是可执行文件。

**如果构建报错**：
- 提示找不到 `CURL` 或 `nlohmann_json` → 检查 vcpkg 路径是否正确
- 提示找不到 `easyx` → 本项目已移除 EasyX，不需要安装；如果旧教程提到 easyx 请忽略

### 5. 运行

双击 `build\Release\Hello_chess.exe`，浏览器会自动打开五子棋界面。

如果浏览器没自动打开，手动访问：

```
http://127.0.0.1:8099/
```

> **注意**：首次运行 Windows 可能会弹出防火墙提示，点"允许"即可。

---

## 二、界面怎么用

### 主界面

- **棋盘**：鼠标点击空位落子
- **侧栏**：显示当前状态、回合、步数、棋手信息
- **按钮**：
  - `New Game` — 开新局
  - `Stats` — 查看全局统计
  - `Undo` — 悔棋
  - `Save` — 存残局
  - `Resumes` — 载入残局
  - `Replays` — 回放已下完的棋局
  - `Abort` — 中止当前对局
  - `Flip` — 翻转棋盘（黑方视角）
  - `Quit` — 退出程序

### 新游戏对话框

- **Board size**：棋盘尺寸（4–30，默认 15）
- **Win length**：连珠数（4–15，默认 5）
- **Black / White**：双方棋手（下拉选择）
- **Enable memory storage**：记忆存储开关（**默认勾选**）。开启后提供：悔棋、存档/回放、残局载入、全局统计；取消勾选则以上功能全部停用（对局本身照常进行）

### 棋手档位说明

| 编号 | 棋手 | 风格 |
|------|------|------|
| 1 | Human | 人类（鼠标点击） |
| 2 | EasyJudge | 最弱：随机 + 堵 |
| 3 | PureGreed 1.0 | 威胁层级攻防共用；常规评分纯防守（只堵不建） |
| 4 | PureGreed 1.1 | 同上；常规评分攻防同权（堵与建比较） |
| 5 | Minimax++ | α-β 搜索 |
| 6 | API AI | 远程大模型（需配置 config.json） |
| 7 | TacticalMax | **最强**：增量评估 + PVS + VCF/VCT |

### 回放模式

点击 `Replays` 选一个棋局，进入回放模式：
- `◀◀` 回到第一步 / `▶▶` 跳到最后一步
- `◀` / `▶` 上一步 / 下一步
- `▶` 播放 / 暂停
- 进度滑块拖动到任意步
- 按 `Exit` 退出回放

### 快捷键

| 键 | 功能 |
|----|------|
| `N` | 新游戏 |
| `U` | 悔棋 |
| `S` | 存残局 |
| `R` | 回放 |
| `T` | 切换浅色/深色主题 |
| `F` | 翻转棋盘 |
| `Esc` | 关闭弹窗 |

---

## 三、命令行模式

如果你不想用浏览器，想在终端里下棋：

```
build\Release\Hello_chess.exe --cli
```

终端模式会用键盘选棋手、鼠标（或键盘方向键）落子。

### 自对弈（测试 AI 棋力）

让两个 AI 自动对弈，输出胜率到 `selfplay.log`：

```
build\Release\Hello_chess.exe --selfplay 10 --p1 7 --p2 5 --budget 1500
```

参数说明：
- `10` — 下 10 局
- `--p1 7` — 黑方用 TacticalMax
- `--p2 5` — 白方用 Minimax++
- `--budget 1500` — 单步思考时间 1500 毫秒

结果会写到 `selfplay.log` 文件里。

---

## 四、目录结构

```
chess/
├── main.cpp              # 程序入口（Web/CLI/自对弈三种模式）
├── core.h / core.cpp     # 棋盘、胜负判定
├── player.h / player.cpp # 棋手基类 + 各 AI 实现
├── threat.h / threat.cpp # 必胜/必防威胁检测
├── ai_config.h / ai_config.cpp  # API 配置加载
├── ai_player.h / ai_player.cpp  # 远程大模型 API 玩家
├── controller.h / controller.cpp # 终端模式主流程
├── storage.h / storage.cpp  # 存档/回放/统计
├── tactical_max.h / tactical_max.cpp # TacticalMax 引擎（最强 AI）
├── server/               # HTTP 服务
│   ├── game_server.cpp   # 路由注册
│   ├── session.cpp       # 对局状态机
│   └── ...
├── web/                  # 前端界面
│   ├── index.html
│   ├── style.css
│   └── app.js
└── build/                # 构建产物
    └── Release/
        └── Hello_chess.exe
```

---

## 五、常见问题

### Q1：双击 exe 后浏览器没打开？

检查 `web` 文件夹是否和 `Hello_chess.exe` 在同一目录。如果 `web` 在上一级目录，把 `web` 文件夹复制到 `build\Release\` 下。

### Q2：提示 "Failed to start Gomoku UI"？

说明 `web/index.html` 没找到。把 `chess\web` 整个文件夹复制到 `build\Release\` 旁边。

### Q3：API AI（6 号）怎么配置？

在 `chess` 目录下新建 `config.json`：

```json
{
  "model": "gpt-4o",
  "apiUrl": "https://api.openai.com/v1/chat/completions",
  "apiKey": "你的 API Key",
  "temperature": 0.7,
  "maxTokens": 256,
  "displayName": "API AI",
  "systemPrompt": "You are a Gomoku player...",
  "userPromptTemplate": "Board:\n{board}\nYou play {color}."
}
```

不配置或 API Key 错误时，6 号会自动回退到 Minimax++。

### Q4：怎么切换深色模式？

点击顶栏的 ◐ 按钮，或按 `T` 键。主题会记住，下次打开还是你选的颜色。

### Q5：构建时报错 "找不到 CURL"？

确认 vcpkg 路径在 `CMakeLists.txt` 第 45 行设置正确，并且已经执行过 `vcpkg install curl nlohmann-json`。

---

## 六、开发者信息

- **语言**：C++17
- **构建系统**：CMake
- **依赖**：curl、nlohmann-json（vcpkg 安装）
- **前端**：纯 HTML/CSS/JS（无框架）
- **AI 档位**：2/3/4/5/7 本地算法，6 远程大模型

---

## 七、更新日志

- **2026-09-29**：存储架构统一（默认开启）
  - `StorageConfig.enabled` 默认 `true`，CLI 不再询问，Web 端默认勾选
  - 删除 Web 会话的平行落子历史 `history_`：**StorageManager 成为唯一事实来源**，悔棋/步数/最后一手标记全部由存储层提供，两套记录不再可能不同步
  - 开关语义统一由 StorageManager 内部守卫：关闭 = 悔棋/回放/残局/统计一律不可用（返回空或 false），对局本身不受影响；修复"不开启也能悔棋"与"载入残局无视开关强制开启"两处不一致
  - 新增 `Board::unset`（与 `place` 配对维护空位计数），悔棋/回放/残局恢复改用 place/unset，修复撤销后 `isFull` 判满失真
- **2026-09-28**：TacticalMax 转正（替代实验棋手 Tactical++）
  - 增量窗口评估，单节点 O(1)
  - 棋型分级查表（活三/眠三 10 倍差）
  - 迭代加深 + PVS + 置换表
  - VCF/VCT 与主搜索共享预算
  - 首手随机统一为中心正方形真随机
  - 新增 `--selfplay` 自对弈模式
  - 前端新增回放控制条、Abort、主题切换、Toast、快捷键

  ## 致谢

- **cpp-httplib**：本地 HTTP 服务：<https://github.com/yhirose/cpp-httplib>
- **libcurl**：API AI 网络请求：<https://curl.se>
- **nlohmann/json**：配置解析与 JSON 处理：<https://github.com/nlohmann/json>
