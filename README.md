# Hello_chess

基于 EasyX 的 Windows 多子棋游戏（C++17 / CMake），默认 15×15 五子棋，支持自定义 N×N 棋盘与 n 连获胜。仅 Windows 平台。

## 游戏规则

- 默认 15×15 五子棋（n=5），黑先白后，横/竖/斜任一方向连成 **n 颗同色** 即胜，棋盘填满未分胜负判平局。
- 启动时控制台输入 `c` 可自定义棋盘尺寸 N（5..30）与连珠数 n（3..N），回车则用默认。

## 棋手类型

| 编号 | 棋手 | 说明 |
|:----:|:-----|:-----|
| 1 | 人类 | 鼠标点击落子 |
| 2 | EasyJudge | 随机 + 防输机制 |
| 3 | PureGreed 1.0 | 纯防守评分 |
| 4 | PureGreed 1.1 | 攻防评分 |
| 5 | Minimax++ | alpha-beta 搜索（最强） |

双方自由组合，**支持 AI 对 AI 观战**。

## AI 决策机制

所有 AI 共用统一决策流程（定义在 `player.cpp` 顶部）：

```
必胜类接管 → 堵对方一步成连 → 堵对方活n-1/活n-2威胁 → 禁用机制(必防位) → 评分/搜索
```

- **必胜类接管**（`findWinMove`）：1 步必胜（自己成 n 连）+ 2 步必胜（下 m 后 ≥2 成连点，且对方无 1 步成连反制），检测到直接返回，跳过后续评分。反制检查避免误报必胜导致不堵对方威胁。
- **活 n-1 / 活 n-2 威胁堵截**（`findOppCriticalThreats`）：模拟对方落子，若形成活四（活 n-1）或活三（活 n-2），精确候选限定为这些威胁位并在其中选最优。解决"不知道堵活三/活四"的低级错误，对任意 n 成立。
- **评分核**：几何级数梯度（活四 ≫ 活三 ≫ 活二），**眠四(50000) > 活三(20000)**，确保"一步成 n 威胁"优先级高于"两步发展威胁"。
- **Minimax++**：alpha-beta 深度 3、半径 2，`val=±1e8` 搜索主导，启发式(/1000)仅打破平局，必胜路线必被选中。

| AI | 策略 |
|:---|:-----|
| EasyJudge / PureGreed 1.0 | 防守评分 |
| PureGreed 1.1 | 防守 + 0.9×进攻 |
| Minimax++ | 搜索主导 + 启发式打破平局 |

> 必胜类（1-2 步）+ minimax 搜索（3 步内）共同覆盖近端必胜，一旦有必胜机会即强制接管。

## 构建与运行

环境：Windows + MSVC(C++17) + CMake ≥ 3.10 + EasyX。

```bash
cmake -B build
cmake --build build --config Release
```

产物 `build/Release/Hello_chess.exe`。EasyX 默认查找 `C:/Program Files (x86)/EasyX/include`、`C:/EasyX/include`，自定义路径用 `cmake-local.cmake`（已 gitignore）设置 `EASYX_LOCAL_PATH`。Minimax++ 深度/半径在 `player.h` 顶部 `constexpr` 调整。

## 已知限制

- 仅 Windows，不支持悔棋、联机对战、棋谱保存。
- Minimax++ 深度 3，稳定识别 3 步内必胜/必败与活四/活三威胁；超过深度的远端必胜可能漏识，复杂局面下仍可能非最优。调大 `kDepth` 可增强棋力（代价是每步变慢）。
