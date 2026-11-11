# 五子棋 Gomoku · 使用与实现手册

双击 `Gomoku.exe`，浏览器自动打开棋盘，直接就能落子。不用装、不用配、不用敲命令。

---

## 一、最快上手

1. 找到 `Gomoku.exe`，双击。
2. 等一两秒，默认浏览器会打开 `http://127.0.0.1:<端口>/`，棋盘已经开好。
3. 你是黑方，鼠标点交叉点落子。电脑白方会在 1 秒内应一手。
4. 下完想关：**关掉那个黑色控制台窗口**，程序就退出了。

浏览器没自动弹出来，就把控制台里显示的网址复制到浏览器。**控制台窗口在，程序就在运行**；
它是有意保留的，进程出问题能一眼看见，别嫌它丑。

### 要带哪些文件

整个 `Release` 目录拷走即可，绿色免安装：

```
Gomoku.exe          主程序
GomokuBench.exe     可选：棋力/回归测试工具
libcurl.dll         必需（API AI 玩家的网络库）
z.dll               必需（libcurl 的依赖）
webapp/             必需：网页界面本体
  index.html
  app.js
  style.css
config.json         可选：只有要用远程大模型玩家才需要
```

另外目标机需要 VC++ 运行库（`MSVCP140.dll` / `VCRUNTIME140.dll`），本项目**不是静态 CRT**，
没装运行库会启动失败。

程序会在 exe 旁边创建 `data/` 存棋谱、残局和统计，删掉不影响运行。

---

## 二、三种界面

```bash
Gomoku.exe                   # 浏览器界面（默认），自动开局
Gomoku.exe --no-start        # 浏览器界面，但先不自动开局
Gomoku.exe --view cli        # 终端字符棋盘
Gomoku.exe --view easyx      # 原生窗口（需要 EasyX）
```

网页起不来（`webapp/` 缺失、端口被占）时，**会自动切到终端界面**，不会只留一个报错黑框；
只有你显式写了 `--view web` 才停下来报错。

### 网页界面怎么用

| 操作 | 位置 |
|---|---|
| 落子 | 点棋盘交叉点 |
| 悔棋 | 顶栏 Undo，或按 `u` |
| 重开 / 换对手 | 顶栏 New game：13/15/19 路预设一格选中，或展开自定义 |
| 存残局 / 读残局 | 右侧 Actions → Save / Load |
| 看历史棋谱并回放 | 右侧 Actions → Replays，进去后可用方向键、空格播放 |
| 换主题 / 翻转棋盘 | 顶栏按钮，或 `t` / `f` |
| 中途换人 | 右侧 Players，选好点 Set（对局进行中可用） |
| 认输 | 右侧 Actions → Resign |
| 退出 | 顶栏电源按钮，或直接关控制台窗口 |

一局结束后，棋盘**下方**会出现结果横幅，可以直接 Rematch（沿用当前设置再来一局）或
Dismiss 收起横幅、保留棋盘复盘。横幅不会遮住刚赢的那五颗子。

快捷键在输入框里不会触发；`Ctrl`、`Alt`、`Cmd` 组合键留给浏览器，不会被抢。

### 终端界面

输入 `行 列` 落子（如 `7 7`，也认 `7,7`），`h` 看帮助，`q` 退出。
命令还有：`u` 悔棋、`n` 新局、`s` 存档、`l <id>` 读档、`a` 中止、`t` 切换存储、
`b <id>` / `w <id>` 换黑白双方、`players` 列出可选玩家。

---

## 三、命令行参数

| 参数 | 说明 |
|---|---|
| `--view <id>` | 界面：`web`（默认）/ `cli` / `easyx` |
| `--black <id>` / `--white <id>` | 双方玩家，见下表 |
| `--board <n>` | 棋盘边长，合法 4–30 |
| `--win <n>` | 连珠数，合法 4–15，且不能超过棋盘边长 |
| `--think <ms>` | AI 单步思考上限，默认 800 |
| `--port <n>` | 网页端口，0 表示自动分配 |
| `--no-start` | 打开后不自动开局 |
| `--no-browser` | 不自动打开浏览器 |
| `--no-storage` | 不写存档 |
| `--data <dir>` | 数据目录，默认 `data` |
| `--list` / `--views` | 列出玩家 / 界面插件后退出 |
| `--selftest` | 端到端自检 |
| `--selfplay <n>` | 无头自对弈 n 局 |
| `--hold <秒>` | 自检后继续托管网页若干秒，便于人工验收 |
| `--quiet` | 少打印 |

内置玩家：

| id | 说明 |
|---|---|
| `human` | 人类，由界面落子 |
| `easy-judge` | 最弱：威胁判定 + 随机 |
| `puregreed-1.0` | 常规评分，纯防守 |
| `puregreed-1.1` | 常规评分，攻防同权 |
| `minimax` | α-β 搜索 + 置换表（默认白方） |
| `tactical-max` | 最强：增量评估 + PVS + VCF/VCT |
| `api-ai` | 远程大模型，需要在 `config.json` 填 key |

老命令行 `--p1 5 --p2 7` 仍然可用，编号会翻译成插件 id。

---

## 四、常见问题

**双击弹出"Windows 已保护你的电脑"**
这是 Windows SmartScreen 对未签名 exe 的提示，不是程序行为。点「仍要运行」；同一个文件
只需确认一次。想彻底消除只能给 exe 做代码签名证书。

**双击后是黑底字符棋盘，不是网页**
说明网页界面没起来（多半是 `webapp/` 不在 exe 旁边），程序已自动降级到终端。
把 `webapp/` 拷到 exe 同级再试。

**提示 "cannot find webapp/index.html"**
同上：`webapp/` 与 exe 不在同一目录，程序会向上找 4 层，找不到就报这个。

**端口被占用**
`Gomoku.exe --port 8099` 换一个，或 `--port 0` 让它自动挑。

**`--view easyx` 报界面不可用**
EasyX 是可选依赖，需要 `easyx.h` 才能编译进来。当前默认不再依赖它；装了 EasyX 后
设置环境变量 `EASYX_LOCAL_PATH` 指向其安装目录再重新构建即可。

**中文目录下网页起不来**
已把 exe 路径改成按 UTF-8 整体转换（原来逐字节截断会乱码）。若仍异常，先把程序放到纯英文路径下确认。

**AI 思考时点了没反应**
AI 在控制线程上同步计算，单步上限 800ms；这一步算完才会执行你排队的命令。
嫌慢就 `--think 300`，或换 `puregreed-1.1` 这类极快的档位。

**存档读回来双方都变成人类**
已知缺陷：存档目前只记录玩家显示名、不记录插件 id，读回残局时会退回 `human`。
临时办法：读档后用 New Game 对话框或侧栏重新指定 AI。这一项待修。

---

## 五、自检与棋力回归

```bash
Gomoku.exe --selftest          # 端到端：内核、存储、界面、HTTP、分层守卫
GomokuBench.exe --selftest     # 引擎层的 9 项算法回归
```

`--selftest` 的退出码：

| 码 | 含义 |
|---|---|
| 0 | 全绿，且所有用例都真的执行了 |
| 1 | 有断言失败 |
| 2 | 有环境跳过的用例（覆盖不完整） |
| 3 | 执行的用例数少于预期（有段被提前跳过） |

自检项数以运行期输出为准，不要在文档里写死。

跑一局棋力对比：

```bash
GomokuBench.exe --games 4 --p1 tactical-max --p2 minimax --budget 800 --out results
```

输出每局胜负、总手数、双方平均/最大单步耗时。

---

## 六、自己构建

需要 CMake 3.15+、支持 C++17 的编译器、`nlohmann_json` 与 `libcurl`（本机用 vcpkg）。
EasyX 可选。

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
```

`CMakeLists.txt` 里 vcpkg 路径是写死的（`D:/SOFT/vcpkg/installed/x64-windows`），换机器要改。
构建完 `webapp/` 与运行时 DLL 会自动拷到 exe 旁边。

没有 EasyX 时加 `-DGOMOKU_WITH_EASYX=OFF` 可跳过该插件，其余功能不受影响。

---

## 七、加一个玩家（不动内核）

```cpp
// 1) plugins/players/MyPlayer.h
class MyPlayer final : public gomoku::IPlayer {
public:
    std::string id() const override { return "my-player"; }
    std::string displayName() const override { return "My Player"; }
    gomoku::Decision tick(const gomoku::ViewState& s,
                          const gomoku::ThinkBudget& budget) override {
        // s 是只读快照；算完返回坐标。务必在 budget 内返回。
        return gomoku::Decision::at(7, 7);
    }
};
```

```cpp
// 2) plugins/PlayerRegistry.cpp 注册一行
reg.add(PlayerInfo{ "my-player", "My Player", "一句话说明", false, true, "" },
        [](const PlayerContext&) -> PlayerPtr { return std::make_unique<MyPlayer>(); });
```

```cmake
# 3) CMakeLists.txt 的 GOMOKU_CORE_SOURCES 里加上源文件
```

重新构建后 `--black my-player` 即可使用，界面下拉框也会自动出现。

## 八、加一个界面（不动内核、不动玩家）

实现 `IView` 的 `render()` 与 `poll()`：`render` 拿只读快照、不许阻塞；`poll` 消费输入。
然后在 `plugins/ViewRegistry.cpp` 加一行 `reg.add(...)`，并把 `.cpp` 加进 CMake。
`--view <你的id>` 即可启动。

## 九、换存储

实现 `IStorageGateway`（`beginGame` / `record` / `finish` / `undo` / `listGames` /
`loadResume` / `globalStats` 等），在宿主里换掉 `makeLegacyStorageGateway()` 的调用即可。
写 JSON、写 SQLite 都行，内核不关心格式。

---

## 十、目录结构

```
contracts/   纯契约：接口与数据类型，只依赖标准库
kernel/      棋盘、棋规、状态机、对局编排、存储边界
app/         JSON 协议、参数解析、界面主循环驱动
plugins/     players（玩家）· views（界面）· storage（存储）
hosts/       cli / web / selfplay / selftest 四种运行形态
engine/      旧版 AI 引擎（经适配器接入，可整体替换）
bench/       棋力与算法回归工具
webapp/      网页界面（index.html + app.js + style.css）
```

依赖只能向下，`contracts/` 里出现上层 include 会被自检的分层守卫拦下。
