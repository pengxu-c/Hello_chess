**简体中文** | [English](README.en.md)

# Hello_chess

五子棋（可有损扩展为多子棋）：基于 EasyX 图形库的 Windows 对弈游戏，**C++17** 编写，面向对象设计，CMake 构建（MSVC）。默认 15×15 五子棋，支持自定义棋盘与连珠数，内置棋局存储管理（悔棋 / 回放 / 残局 / 统计）与远程大模型 API 棋手。

> 本项目面向 Windows 平台，依赖 EasyX 图形库。

---

## 快速上手

### 1. 环境要求

| 项目 | 要求 |
|:-----|:-----|
| 操作系统 | Windows 10 / 11 |
| 编译器 | MSVC（Visual Studio 2019/2022，勾选"使用 C++ 的桌面开发"） |
| 构建工具 | CMake ≥ 3.15 |
| 包管理 | vcpkg（安装 curl、nlohmann-json） |
| 图形库 | EasyX（手动安装） |

### 2. 安装依赖

vcpkg 安装网络与 JSON 库：

```bash
vcpkg install curl:x64-windows nlohmann-json:x64-windows
```

EasyX 从官网 <https://easyx.cn> 下载安装，这个作者的下载到了msvc对应位置，故而已经方便配置好了环境。

### 3. 配置 EasyX 路径（可选）

`CMakeLists.txt` 默认在以下位置查找 EasyX：

- `C:/Program Files (x86)/EasyX/include`
- `C:/EasyX/include`

若安装在其他位置，在仓库根目录创建 `cmake-local.cmake`（已被 `.gitignore` 忽略，不会提交）：

```cmake
set(EASYX_LOCAL_PATH "D:/your/path/to/EasyX")
```

### 4. 构建步骤

```bash
cmake -B build -S .
cmake --build build --config Release
```

构建产物为 `build/Release/Hello_chess.exe`。

> 运行时若出现 `libpng warning: iCCP: known incorrect sRGB profile` 警告可忽略，不影响功能。

### 5. 运行与首次对弈流程

运行 `build/Release/Hello_chess.exe`，按控制台提示逐步操作：

**第 1 步：规则配置**

```
Customize rules? Type 'c' to customize, or press Enter for default (15x15, 5-in-a-row):
```

- **直接回车** → 使用默认 15×15 棋盘、五连珠，**跳过第 2、3 步**（记忆存储保持关闭）。
- **输入 `c`** → 进入自定义流程，继续第 2 步。

**第 2 步：自定义规则（仅输入 `c` 时）**

```
Win length k (4..15, default 5):       ← 连珠获胜数，回车默认 5
Board size N (k..30, default 15):      ← 棋盘边长，回车默认 15
```

输入非法或越界时自动回退默认值。

**第 3 步：记忆存储开关（仅输入 `c` 时）**

```
Enable memory storage? (y/n, default n):
```

- `y` → 开启存储，数据写入 `data/` 目录，并解锁游戏中控制台命令与主菜单存储选项。
- `n` 或回车 → 保持关闭，纯对弈模式。

**第 4 步：选择双方棋手**

```
Choose player 1 (Black, first) type number:    ← 玩家 1，执黑先手
Choose player 2 (White, second) type number:   ← 玩家 2，执白后手
```

编号 1-7 含义见 [棋手类型](#棋手类型)；输入非数字或越界时默认为人类玩家。选择 6 时若 `config.json` 未配置，自动回退为玩家 5 并在控制台提示。

**第 5 步：开始对弈**

- 人类玩家在图形窗口**鼠标点击**落子，黑先白后轮流。
- 存储开启时，可随时在**控制台**输入命令（`help` 查看所有命令，如悔棋、保存残局）。
- 率先连成 k 子者获胜，棋盘填满则平局，结果以弹窗提示。

**第 6 步：对局结束**

- 弹窗询问 `Play again?` → 同一棋手组合连续对局（棋盘自动清空）。
- 关闭窗口后控制台询问 `Return to main menu?` → `y` 回主菜单重新配置棋手。

**第 7 步：主菜单存储选项（仅存储开启时出现）**

```
=== Storage Options ===
  1. New game                       新对局
  2. Load resume (continue saved game)   载入残局续弈
  3. Replay saved game              逐步回放历史棋局
  4. Show global statistics         查看全局统计
```

选择 3 / 4 后会询问是否返回主菜单；回放操作键：`Space`=下一步、`B`=上一步、`Home`=首、`End`=末、`ESC`=退出。

---

## 功能速览

### 棋手类型

| 编号 | 棋手 | 说明 |
|:----:|:-----|:-----|
| 1 | Human | 人类玩家，鼠标落子 |
| 2 | EasyJudge | 随机 + 防输 |
| 3 | PureGreed 1.0 | 纯防守评分 |
| 4 | PureGreed 1.1 | 攻防评分 |
| 5 | Minimax++ | αβ 搜索（最强） |
| 6 | API AI | 远程大模型，需配置 |
| 7 | Tactical++ | 实验棋手 |

> 难度名称仅作相对区分，不代表实际棋力。

### 记忆存储

仅在自定义规则流程（输入 `c`）中询问开关，**默认关闭**。开启后支持：`undo` 悔棋、`save` 存残局、残局续弈、棋局回放、跨局统计、异常退出自动保存。游戏中控制台命令（`h`=help、`ls`=list、`s`=save、`u`=undo、`a`=abort、`st`=stats 等，`help` 查看全部）。数据写入 `data/`（`games/`、`resumes/`、`stats.txt`），`key=value` 文本格式，向后兼容。

### API AI（玩家 6）

通过 HTTP 调用任意 OpenAI 兼容接口落子；未配置或出错时**自动回退玩家 5**，不影响程序运行。使用方法：按 `config.example.json` 格式补填 `config.json` 中的 `api_url`、`api_key`、`model` 三个必填字段即可（`display_name` 留空自动显示模型名，其余字段模板已配好）。`config.json` 含密钥已被 `.gitignore` 忽略。

---

## 架构设计

### 类设计

| 文件 | 类 | 职责 |
|:-----|:---|:-----|
| `core.h/.cpp` | `Board` | 棋盘数据（`std::vector` 一维），持有 size/winLen/emptyCount，落子/读取/O(1)判满 |
| | `Judge` | 胜负判定，复用 `scanLine` 统一连珠统计 |
| | `scanLine` | 公共 inline 线段扫描核，供 Judge 与各 AI 共用 |
| | `Stats` | 数据统计（双方步数） |
| `ui.h/.cpp` | `UI` | **封装全部 EasyX 调用**，持有布局参数（gridSize/xOffset/yOffset/boardSize） |
| `threat.h/.cpp` | `ThreatDetector` | **必胜/必防威胁检测**：成连位计数、1/2 步必胜、必防、双活三创建位，供各 AI 复用 |
| `player.h/.cpp` | `Player` | 棋手抽象基类 |
| | `HumanPlayer` | 人类，派生自 Player |
| | `GreedyScoringAI` | **通用评分 AI**，攻防权重参数化，一次实现覆盖多档难度 |
| | `MinimaxPP` | alpha-beta 搜索 AI，派生自 Player |
| | `APIPlayer` | 远程大模型 API AI，派生自 Player |
| `ai_config.h/.cpp` | `AIConfig` | API AI 配置加载（JSON 驱动） |
| `storage.h/.cpp` | `StorageManager` | **棋局存储管理**：悔棋、回访、残局、统计、控制台命令 |
| `controller.h/.cpp` | `GameController` | 主循环、规则配置、终端选玩家、回合调度、存储集成 |
| `main.cpp` | — | 仅构造控制器并 `run()` |

### 类关系

- **全局可变状态已消除**：棋盘尺寸/连珠数由 `Board` 成员持有，布局参数由 `UI` 成员持有，支持多棋盘并存、易于测试。
- `scanLine`（core.h inline）为统一线段统计核，`Judge::checkWin`、`ThreatDetector`（threat.cpp）与 player.cpp 的 `pointScore` 共用，单一实现。
- `Player` 为抽象基类，各棋手**派生**自它，统一 `place()` 接口；`GreedyScoringAI` 通过构造参数实例化不同难度档，新增档位无需改类。
- `GameController` **组合** `Board`/`Judge`/`Stats`/`StorageManager`（值语义）与 `UI*`、两个 `Player*`（堆，析构释放）。
- `StorageManager` 独立管理持久化：接口分离、`StorageConfig` 配置驱动、`key=value` 格式可扩展、命令模式可扩展新命令、前置声明减少耦合。
- EasyX 相关调用集中在 `UI` 类，未来替换图形库只需改此类。

### Minimax++ 设计要点

极小化极大搜索 + Alpha-Beta 剪枝（深度 4，`player.h` 顶部 `kDepth` 可调），配合：启发式排序、Zobrist 置换表、静态缓冲、`segValue` 统一评分表、邻域扫描（radius=2）、O(1) 判满。决策流程：必胜/必防层级（`ThreatDetector`）→ 己方1步必胜 → 堵对方1步成连 → 己方2步必胜 → 防守候选取并集（对方2步必胜第一步位 ∪ 双活三创建位）→ 候选 minimax 选最优。越浅层获胜分越多，优先最快取胜路径。

### 目录结构

```
chess/
├── core.h / core.cpp              Board, Judge, Stats, Pos, ChessType
├── ui.h / ui.cpp                  UI（EasyX 封装）
├── player.h / player.cpp          Player 基类 + 派生棋手
├── ai_config.h / ai_config.cpp    AIConfig（API AI JSON 配置加载）
├── ai_player.h / ai_player.cpp    APIPlayer（远程大模型 API 玩家）
├── storage.h / storage.cpp        StorageManager（悔棋/回访/残局/统计/命令）
├── controller.h / controller.cpp  GameController（规则配置 + 回合调度 + 存储集成）
├── main.cpp                       程序入口
├── experimental_tactical_player.h / .cpp   实验棋手
├── CMakeLists.txt                 CMake 构建脚本
├── cmake-local.cmake              本地 EasyX 路径（已被 .gitignore 忽略）
└── data/                          运行时自动创建的数据目录
    ├── stats.txt                  全局统计
    ├── games/                     棋局记录
    └── resumes/                   棋局残局
```

---

## 致谢

- **EasyX** 图形库：全部图形绘制与交互实现：<https://easyx.cn>
- **libcurl**：API AI 网络请求：<https://curl.se>
- **nlohmann/json**：配置解析与 JSON 处理：<https://github.com/nlohmann/json>
