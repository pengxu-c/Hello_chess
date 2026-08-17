# Hello_chess

一个基于 EasyX 图形库的 Windows 多子棋游戏，使用 **C++17** 编写，面向对象设计，通过 CMake 构建。默认为 15×15 五子棋，支持自定义 N×N 棋盘与 n 连获胜。

> 本项目仅面向 Windows 平台，依赖 EasyX 图形库，不跨平台。

---

## 游戏规则

- 默认棋盘规模：15 × 15，连珠获胜数：5（五子棋）。
- 对局双方：黑棋先手，白棋后手，轮流落子。
- 胜利条件：率先在横、竖、斜任一方向上形成 **连续 n 颗同色棋子** 者获胜。
- 平局：棋盘填满仍无人达成 n 连，判为平局。
- **自定义规则**：启动时控制台提示 `Customize rules?`，输入 `c` 可设置棋盘尺寸 N（5..30）与连珠数 n（3..N）；直接回车则使用默认 15×15 五子棋。

## 棋手类型

启动后在控制台分别为**玩家1**与**玩家2**选择棋手类型（玩家1 执黑先手，玩家2 执白后手）：

| 编号 | 棋手 | 说明 |
|:----:|:-----|:-----|
| 1 | 人类玩家 | 鼠标点击落子 |
| 2 | EasyJudge | 超简单：随机落子 + 防输机制 |
| 3 | PureGreed 1.0 | 纯防守评分 |
| 4 | PureGreed 1.1 | 攻防评分 |
| 5 | Minimax++ | alpha-beta 剪枝 + 启发式排序 + Zobrist 置换表（最强） |

任意双方可自由组合，**支持 AI 对 AI**（如玩家1 选 Minimax++，玩家2 选 EasyJudge，即可观战 AI 互弈）。输入非数字或越界时默认为人类玩家。

> AI 难度名称仅作相对区分，不代表实际棋力水平，请以体验为准。

## 类设计



| 文件 | 类 | 职责 |
|:-----|:---|:-----|
| `core.h/.cpp` | `Board` | 棋盘数据，落子/读取/判满 |
| | `Judge` | 胜负判定（连珠长度参数化） |
| | `Stats` | 数据统计（双方步数） |
| `ui.h/.cpp` | `UI` | **封装全部 EasyX 调用**：窗口、渲染、鼠标、消息框 |
| `player.h/.cpp` | `Player` | 棋手抽象基类 |
| | `HumanPlayer` | 人类，派生自 Player |
| | `EasyJudgeAI` | 超简单 AI，派生自 Player |
| | `PureGreed10` | 防守 AI，派生自 Player |
| | `PureGreed11` | 攻防 AI，派生自 Player |
| | `MinimaxPP` | alpha-beta 搜索 AI，派生自 Player |
| `controller.h/.cpp` | `GameController` | 主循环、规则配置、终端选玩家、回合调度 |
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
├── controller.h / controller.cpp  GameController（规则配置 + 回合调度）
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

## Minimax++ 算法详解（最强 AI）

Minimax++ 采用极小化极大搜索 + Alpha-Beta 剪枝，并集成多项性能与正确性优化：

### 搜索框架
- **Alpha-Beta 剪枝**：深度优先搜索，剪去不可能影响决策的分支。
- **搜索深度 4**（可在 `player.h` 顶部 `kDepth` 调整），配合下列优化可在合理时间内完成。
- **胜负距离加权**：越浅层获胜分越多，促使 AI 优先选择最快取胜 / 最迟告负的路径。

### 性能优化
| 技术 | 说明 |
|:-----|:-----|
| 启发式排序 | 搜索前用 `pointScore` 对候选着法降序排序，优先搜索高分节点，剪枝效率提升 10-100 倍 |
| Zobrist 置换表 | 64 位哈希标识局面，`unordered_map` 存储已搜索结果，避免重复计算相同局面 |
| 静态缓冲 | `generateMoves` 用静态布尔数组去重，避免递归中堆分配 `vector<vector<bool>>` |
| 统一评分表 | `evaluate` 与 `pointScore` 共用 `segValue` 评分核，消除量纲不一致 |

### 正确性保障
- **必胜类接管**：先检测 1 步 / 2 步必胜，命中直接落子。
- **对方一步成连**：检测对方下一步成连位，必须立即堵。
- **防守候选取并集**：活三 / 活四威胁（`critical`）与眠四 / 冲四必防（`must`）取**并集**而非互斥选择，修复旧版"同时存在活三与眠四时只防活三、被眠四连五杀"的致命漏洞。
- **评分几何级数**：活四 ≫ 活三 ≫ 活二，优先级严格单调，对任意 WIN_LEN(3..N) 成立。

### 决策流程
```
place():
  1. 必胜类（1步/2步必胜）→ 命中返回
  2. 对方一步成连 → 命中返回堵点
  3. 合并防守候选 = critical ∪ must（去重）
  4. 候选 = 防守候选非空 ? 防守候选 : 全部邻域空位
  5. 对每个候选做 minimax(深度3) + 启发式打破平局，选最优
```

---

## 已知限制

- 仅支持 Windows，目前未做跨平台适配。
- 目前不支持悔棋、联机对战、棋谱保存等功能。
- EasyJudge / PureGreed 系列为启发式评分；Minimax++ 采用 alpha-beta 剪枝搜索（深度 4、半径 2，可在 `player.h` 顶部 `constexpr` 调整），复杂局面下仍可能非最优。
- 棋盘背景图路径写死为相对路径，需从工作目录正确启动方可加载！
- 评估函数为全盘扫描（O(N²)），未做增量评估；置换表在每次顶层决策时清空。

## 致谢

特别感谢 **EasyX** 图形库，本项目的全部图形绘制与交互实现均建立在它之上。

- EasyX 官网：<https://easyx.cn>
