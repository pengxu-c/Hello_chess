# Hello_chess

一个基于 EasyX 图形库的 Windows 六子棋（连六）游戏，使用 C++11 编写，通过 CMake 构建。（目前仅仅用了C特性）

> 本项目仅面向 Windows 平台，依赖 EasyX 图形库，不跨平台。

---

## 游戏规则

- 棋盘规模：15 × 15。
- 对局双方：黑棋先手，白棋后手，轮流落子。
- 胜利条件：率先在横、竖、斜任一方向上形成 **连续 6 颗同色棋子** 者获胜。
- 特别说明：**五连不胜**，必须形成六连（或更长连珠）才算获胜。
- 平局：棋盘填满仍无人达成六连，判为平局。

## 游戏模式

启动后在控制台选择模式（输入非数字或超出范围时默认进入双人对战）：

| 选项 | 模式 | 说明 |
|:----:|:-----|:-----|
| 1 | 双人对战 (PVP) | 两位玩家本地对弈 |
| 2 | 人机对战 · 超简单 | AI 落子逻辑最简单 |
| 3 | 人机对战 · 简单 | AI 具备基础防输机制 |
| 4 | 人机对战 · 困难 | AI 采用攻防评分策略 |

> AI 难度名称仅作相对区分，不代表实际棋力水平，请以体验为准。

## 目录结构

```
chess/
├── main.cpp            # 主入口与主循环
├── game.h              # 数据结构与函数声明
├── game_init.cpp       # 图形窗口与游戏状态初始化
├── game_render.cpp     # 棋盘、棋子与提示绘制
├── game_update.cpp     # 鼠标移动与点击处理
├── game_judge.cpp      # 六连 / 五连判定
├── game_ai.cpp         # AI 落子策略
├── game_mode.cpp       # 控制台模式选择
├── CMakeLists.txt      # CMake 构建脚本
└── cmake-local.cmake   # 本地 EasyX 路径（已被 .gitignore 忽略）
```

## 构建与运行

### 环境要求

- 操作系统：Windows
- 编译器：支持 C++11 的 MSVC / MinGW
- 构建工具：CMake ≥ 3.10
- 图形库：EasyX（需提前安装）

### 配置 EasyX 路径（可选）

`CMakeLists.txt` 默认在以下位置查找 EasyX：

- `C:/Program Files (x86)/EasyX`
- `C:/EasyX`

若你的 EasyX 安装在其他位置，可在仓库根目录创建 `cmake-local.cmake`（该文件已被 `.gitignore` 忽略，不会提交）：

```cmake
set(EASYX_LOCAL_PATH "D:/your/path/to/EasyX")
```

### 构建步骤

```bash
cmake -B build
cmake --build build --config Release
```

构建产物为 `build/Hello_chess.exe`（或对应配置目录下）。

### 背景图（可选）

程序运行时会检测 `Resource/images/bk.jpg`，若存在则作为背景加载，否则使用默认浅棕色背景。该资源目录不包含在仓库中，可按需自行放置。

## 已知限制

- 仅支持 Windows，未做跨平台适配。
- 不支持悔棋、联机对战、棋谱保存等功能。
- AI 思路为启发式评分，无搜索算法，复杂局面下可能并非最优解。
- 棋盘背景图路径写死为相对路径，需从工作目录正确启动方可加载。

## 致谢

本项目使用 [EasyX](https://easyx.cn/) 图形库（免费版），版权归原作者所有，仅供学习使用。
特别感谢 **EasyX** 图形库，本项目的全部图形绘制与交互实现均建立在它之上。

- EasyX 官网：<https://easyx.cn>