# Hello_chess

一个基于 EasyX 图形库的 Windows 六子棋（连六）游戏，使用 **C++17** 编写，面向对象设计，通过 CMake 构建。

> 本项目仅面向 Windows 平台，依赖 EasyX 图形库，不跨平台。

---

## 游戏规则

- 棋盘规模：15 × 15。
- 对局双方：黑棋先手，白棋后手，轮流落子。
- 胜利条件：率先在横、竖、斜任一方向上形成 **连续 6 颗同色棋子** 者获胜。
- 特别说明：**五连不胜**，必须形成六连（或更长连珠）才算获胜。
- 平局：棋盘填满仍无人达成六连，判为平局。

## 棋手类型

启动后在控制台分别为**玩家1**与**玩家2**选择棋手类型（玩家1 执黑先手，玩家2 执白后手）：

| 编号 | 棋手 | 说明 |
|:----:|:-----|:-----|
| 1 | 人类玩家 | 鼠标点击落子 |
| 2 | EasyJudge | 超简单：随机落子 + 防输机制 |
| 3 | PureGreed 1.0 | 纯防守评分 |
| 4 | PureGreed 1.1 | 攻防评分（最强） |

任意双方可自由组合，**支持 AI 对 AI**（如玩家1 选 PureGreed 1.1，玩家2 选 EasyJudge，即可观战 AI 互弈）。输入非数字或越界时默认为人类玩家。

> AI 难度名称仅作相对区分，不代表实际棋力水平，请以体验为准。

## 类设计



| 文件 | 类 | 职责 |
|:-----|:---|:-----|
| `core.h/.cpp` | `Board` | 棋盘数据，落子/读取/判满 |
| | `Judge` | 胜负判定（六连/五连，长度参数化） |
| | `Stats` | 数据统计（双方步数） |
| `ui.h/.cpp` | `UI` | **封装全部 EasyX 调用**：窗口、渲染、鼠标、消息框 |
| `player.h/.cpp` | `Player` | 棋手抽象基类 |
| | `HumanPlayer` | 人类，派生自 Player |
| | `EasyJudgeAI` | 超简单 AI，派生自 Player |
| | `PureGreed10` | 防守 AI，派生自 Player |
| | `PureGreed11` | 攻防 AI，派生自 Player |
| `controller.h/.cpp` | `GameController` | 主循环、终端选玩家、回合调度 |
| `main.cpp` | — | 仅构造控制器并 `run()` |

### 类关系

- `Judge` 是 `Board` 的**友元**，直接读取棋盘内部数据判定连珠。
- `Player` 为抽象基类，四个棋手**派生**自它，统一 `place()` 接口。
- `HumanPlayer` 持有 `UI&` 引用获取鼠标输入；`EasyJudgeAI` 持有 `Judge&`、`Stats&` 引用。
- `GameController` **组合** `Board`/`Judge`/`Stats`（值语义）与 `UI*`、两个 `Player*`（堆，析构释放）。
- EasyX 相关调用集中在 `UI` 类，未来替换图形库只需改此类。

## 目录结构

```
chess/
├── core.h / core.cpp        Board, Judge, Stats, Pos, ChessType
├── ui.h / ui.cpp            UI（EasyX 封装）
├── player.h / player.cpp    Player 基类 + 4 个派生棋手
├── controller.h / controller.cpp  GameController （中文输出乱码问题😭）
├── main.cpp                 程序入口
├── CMakeLists.txt           CMake 构建脚本
└── cmake-local.cmake        本地 EasyX 路径（已被 .gitignore 忽略）
```

## 构建与运行

### 环境要求

- 操作系统：Windows
- 编译器：支持 C++17 的 MSVC
- 构建工具：CMake ≥ 3.10
- 图形库：EasyX（需提前安装）

### 配置 EasyX 路径（可选）

`CMakeLists.txt` 默认在以下位置查找 EasyX 头文件：

- `C:/Program Files (x86)/EasyX/include`
- `C:/EasyX/include`

若你的 EasyX 安装在其他位置，可在仓库根目录创建 `cmake-local.cmake`（该文件已被 `.gitignore` 忽略，不会提交）：

```cmake
set(EASYX_LOCAL_PATH "D:/your/path/to/EasyX")
```

### 构建步骤

```bash
cmake -B build
cmake --build build --config Release
```

构建产物为 `build/Release/Hello_chess.exe`。

### 背景图（可选）

程序运行时会检测 `Resource/images/bk.jpg`，若存在则作为背景加载，否则使用默认浅棕色背景。该资源目录不包含在仓库中，可按需自行放置。

## 已知限制

- 仅支持 Windows，未做跨平台适配。
- 不支持悔棋、联机对战、棋谱保存等功能。
- AI 思路为启发式评分，无搜索算法，复杂局面下可能并非最优解。
- 棋盘背景图路径写死为相对路径，需从工作目录正确启动方可加载。

## 致谢

特别感谢 **EasyX** 图形库，本项目的全部图形绘制与交互实现均建立在它之上。

- EasyX 官网：<https://easyx.cn>
