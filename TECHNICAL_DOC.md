# Hello_chess 技术文档

## 项目概述

基于 EasyX 图形库的 Windows 五子棋游戏，C++17 编写，面向对象设计，CMake 构建。

- 默认 15×15 棋盘，五子连珠获胜；可在启动时自定义 N×N（5..30）棋盘与 n 连（3..N）获胜。
- 支持 5 种棋手类型，可人机对战或 AI 对 AI。

```
chess/
├── main.cpp                 程序入口
├── core.h / core.cpp        Board / Judge / Stats 核心数据与逻辑
├── ui.h / ui.cpp            UI（EasyX 封装：窗口/渲染/鼠标/消息框）
├── player.h / player.cpp    Player 基类 + HumanPlayer + 4 个 AI
├── controller.h / controller.cpp  GameController（规则配置 + 回合调度）
├── CMakeLists.txt           CMake 构建脚本
└── README.md
```

## 技术栈与构建

| 项目 | 说明 |
|:-----|:-----|
| 语言 / 编译器 | C++17 / MSVC（`/utf-8`） |
| 构建系统 | CMake ≥ 3.10 |
| 图形库 | EasyX（Windows 原生），系统库 gdi32/ole32/oleaut32/winmm/msimg32 |

```bash
cmake -B build
cmake --build build --config Release      # 产物 build/Release/Hello_chess.exe
```

## 架构设计

- **策略模式**：`Player` 抽象基类，5 种落子策略由派生类实现。
- **组合模式**：`GameController` 组合 `Board`/`Judge`/`Stats`（值语义）与 `UI*`、两个 `Player*`（堆）。
- **友元优化**：`Judge` 为 `Board` 友元，直接访问 `map_` 加速判定。
- **依赖注入**：构造函数注入 `Judge&`/`Stats&`/`UI&` 等依赖。

```
main.cpp → GameController(控制层)
              ├─ UI 层(渲染/鼠标)
              ├─ Player 层(策略：Human + 4 AI)
              └─ Core 层(Board / Judge / Stats)
```

## 全局变量与枚举（core.h）

```cpp
inline int ROWS = 15, COLS = 15;     // 棋盘尺寸
inline int WIN_LEN = 5;              // 连珠获胜数
inline int GRID_SIZE = 38;           // 每格像素
inline int XOFFSET = 213, YOFFSET = 34;  // 棋盘左上角偏移

enum class ChessType { None = 0, Black = 1, White = -1 };
inline ChessType opponent(ChessType c);   // 取对手颜色（取负）

struct Pos { int r = -1, c = -1; bool valid() const; };
```

> `inline` 变量利用 C++17 特性跨翻译单元共享唯一实例，配置时由 `GameController::configureRules()` 统一更新。

## 核心类

### Board（core.h / core.cpp）

持有 `int** map_` 二维棋盘（0=空, 1=黑, -1=白），`int size_` 与全局 `ROWS` 同步。

| 方法 | 说明 |
|:-----|:-----|
| `place(r,c,color)` | 落子（校验空位，越界或已占用返回 false） |
| `set(r,c,color)` | 直接设置（AI 模拟用，不校验空位） |
| `at(r,c)` / `inBounds(r,c)` / `isFull()` | 读取 / 越界判断 / 判满 |
| `clear()` / `resize()` | 清空 / 按当前 ROWS 重建 |

禁止拷贝（持有裸指针，防浅拷贝双重释放）。

### Judge（core.h / core.cpp）

- `checkWin(b, last, color, length)`：从最后落子点沿 4 方向（右/下/右下/左下），枚举 `length` 个起点偏移调用 `checkLine()` 判定连珠。复杂度 O(4·length²)。
- `checkLine(b, r, c, dr, dc, chess, length)`：从 `(r,c)` 沿 `(dr,dc)` 检查连续 `length` 个 `chess`。

### Stats（core.h / core.cpp）

记录双方步数 `black_`/`white_`，提供 `reset()`/`recordMove()`/`count()`/`printProgress()`（后者清屏打印黑方步数）。

## UI 类（ui.h / ui.cpp）

封装全部 EasyX 调用，便于未来替换图形库。

- **窗口**：`initWindow(w,h)` / `close()`。
- **鼠标**：`pollMouse()` 轮询移动（更新悬停）与左键点击；`hoverPos()`/`clickPos()`/`hasClick()`/`clearClick()`。
- **渲染**：`render(board, hover)` 批量绘制背景→网格→棋子→悬停圈。
- **杂项**：`messageBox()` / `askYesNo()` / `loadBackground()`。
- 私有 `pixelToCell()` 将像素坐标映射到最近格子（距中心 < GRID_SIZE/2 即命中）。

## Player 类层次（player.h / player.cpp）

```
Player (抽象基类)
├── HumanPlayer      鼠标点击落子
├── EasyJudgeAI      随机 + 防输（对方步数<60 时防 WIN_LEN-1 连）
├── PureGreed10      纯防守评分
├── PureGreed11      攻防评分（区分活/死棋）
└── MinimaxPP        alpha-beta 剪枝搜索
```

统一接口：`place(board, color)` 返回落子位置；`isHuman()` / `needsDelay()` / `name()`。

### 评分函数

`threatScore(count)`（PureGreed10 用）与 `windowScore(count)`（MinimaxPP 用），按 `WIN_LEN - count` 分级：

| diff = WIN_LEN - count | threatScore | windowScore |
|:-----------------------|:-----------:|:-----------:|
| 0 (已成连) | 1,000,000 | 1,000,000 |
| 1 (活四) | 40,000 | 50,000 |
| 2 (活三) | 20,000 | 5,000 |
| 3 (活二) | 2,000 | 500 |
| 4 (活一) | 200 | 50 |

### PureGreed10

对每个空位沿 4 方向扫描对方正/反连续子数，累加 `threatScore()`，选总分最高者。只防不攻。

### PureGreed11

对每个空位沿 4 方向同时扫描己方/对方连续子数与被堵端数（`diew[]`/`dieb[]`），按活棋/死棋分级计算 `selfScore + blockScore`，选总分最高者。攻防兼备。

### MinimaxPP

| 配置 | 默认 | 说明 |
|:-----|:-----|:-----|
| `kDepth` | 3 | 搜索深度 |
| `kRadius` | 2 | 候选着法半径 |
| `kInf` | 100,000,000 | 无穷大哨兵 |

- `generateMoves(board)`：若棋盘空返回中心；否则收集已有棋子周围 `kRadius` 内的空位。
- `evaluate(board, aiColor)`：沿 4 方向扫描所有连续同色线段，按长度+两端开放数评分（活棋/眠棋/死棋分级），己方加分、对方减分。详见下方"评估算法"。
- `minimax(board, depth, α, β, curColor, isMax, aiColor)`：alpha-beta 递归；落子后 `checkWin()` 命中则立即返回 `±kInf ∓ (kDepth - depth)`（越浅赢/输分越多）；`α≥β` 剪枝。
- `place()`：①自己能成五→直接获胜；②对方能成五→在该位置防守；③否则 alpha-beta 搜索选评估最高者。

### 评估算法（evaluate）

沿 4 方向（右/下/右下/左下）扫描每段连续同色棋子，按 `count`(连续长度) 与 `openEnds`(两端空位数) 评分：

| diff=WIN_LEN-count | 两端开放(活) | 一端开放(眠) | 两端封堵(死) |
|:---|:---|:---|:---|
| 0 (已成连) | 1,000,000 | 1,000,000 | 1,000,000 |
| 1 (四连) | 200,000 | 20,000 | 0 |
| 2 (三连) | 20,000 | 2,000 | 0 |
| 3 (二连) | 2,000 | 200 | 0 |
| 4 (一连) | 200 | 20 | 0 |
| ≥5 | 20 | 2 | 0 |

己方线段加分，对方线段减分。`diff` 由 `WIN_LEN` 计算，适配任意连珠规则。

## GameController（controller.h / controller.cpp）

| 成员 | 说明 |
|:-----|:-----|
| `board_`/`judge_`/`stats_` | 值语义组合 |
| `ui_`/`player1_`/`player2_` | 堆分配，析构释放 |

- `run()`：`selectPlayers()` → `initWindow(960,600)` → 循环 `playOneGame()` 并询问重开 → `close()`。
- `configureRules()`：输入 `c` 自定义棋盘 N 与连珠 k，按 N 计算 `GRID_SIZE`/`XOFFSET`/`YOFFSET` 使棋盘居中 960×600，并 `board_.resize()`。
- `createPlayer(choice)`：1=Human, 2=EasyJudge, 3=PureGreed1.0, 4=PureGreed1.1, 5=Minimax++，越界默认 Human。
- `playOneGame()`：黑先手，每回合 `needsDelay()` 时 `Sleep(500)`，`pollMouse()` → `place()` → 校验空位 → 落子 → `checkWin()`/`isFull()` 判定 → 切换回合 → `render()`。

## 类依赖关系

```
GameController ──值──▶ Board ◀──友元── Judge
              ──值──▶ Stats
              ──指针─▶ UI, Player*
HumanPlayer ──引用──▶ UI
EasyJudgeAI ──引用──▶ Judge, Stats
MinimaxPP   ──引用──▶ Judge
PureGreed10/11     无外部依赖
```

## AI 算法对比

| AI | 策略 | 复杂度 | 特点 |
|:---|:-----|:-------|:-----|
| EasyJudgeAI | 防守+随机 | O(N²) | 最弱，随机成分大 |
| PureGreed10 | 纯防守评分 | O(N²·4) | 只防不攻 |
| PureGreed11 | 攻防评分 | O(N²·4) | 攻防兼备，区分活/死棋 |
| MinimaxPP | α-β 搜索 | O(M^d) | 最强，M=候选数, d=深度 |

## 配置与扩展

### 调整 MinimaxPP 难度（player.h）

```cpp
static constexpr int kDepth = 3;      // 搜索深度（增大更强更慢）
static constexpr int kRadius = 2;     // 候选半径（增大更广更慢）
```

| 难度 | kDepth | kRadius | 备注 |
|:-----|:-------|:--------|:-----|
| 简单 | 2 | 1 | 快速 |
| 普通 | 3 | 2 | 默认 |
| 困难 | 4 | 2 | 1-3 秒 |
| 专家 | 4 | 3 | 3-10 秒 |

### 添加新 AI

1. `player.h` 继承 `Player` 声明新类。
2. 实现纯虚 `place()`/`isHuman()`/`name()`。
3. `GameController::createPlayer()` 注册新编号。

### 替换图形库

所有 EasyX 调用集中在 `UI` 类，保持公共接口不变即可替换为 SDL/SFML 等。

### 扩展建议

| 功能 | 实现位置 |
|:-----|:---------|
| 悔棋 | GameController 存落子历史栈 |
| 棋谱保存 | GameController 记录每步到文件 |
| 联机对战 | 新增 NetworkPlayer |
| 禁手规则 | Judge::checkWin 增加禁手判断 |

---

## 变更日志

### MinimaxPP 评估函数重构

**旧算法（窗口计数法，已废弃）**：把棋盘拆成所有可能的 `WIN_LEN` 长度窗口，沿横竖正斜反斜四方向扫描。只看"纯色窗口"：全己方加分、全对方扣分、双方混合则忽略。按距成连差步数分级加权——差1加50000(活四)、差2加5000、差3加500、差4加50、已成连加1000000。本质是快速而粗糙的全局趋势评分器，不看棋形死活，只看数量趋势，胜在简单稳定跑得快。缺陷：把 `X.X.X` 与 `XXX..` 评成等价，无法区分活棋/眠棋/死棋，导致 AI 高估分散棋子、低估连续威胁，两 Minimax 对战时几步即败。

**新算法（连续线段法）**：改为扫描连续同色线段，按长度+两端开放数区分活/眠/死棋精确评分；`place()` 增加对方成五防守检查。详见上方"评估算法"。
