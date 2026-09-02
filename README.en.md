[简体中文](README.md) | **English**

# Hello_chess

Gomoku (losslessly extensible to multi-in-a-row games)

A Windows multi-in-a-row board game based on the EasyX graphics library, written in **C++17** with object-oriented design and built via CMake (MSVC compiler recommended). Defaults to 15×15 Gomoku, with support for custom N×N boards and n-in-a-row wins. Includes a built-in **game record storage system** (undo, replay, resume save/load, global statistics).

> This project targets the Windows platform and depends on the EasyX graphics library.

---

## Game Rules

- Default board size: 15 × 15, win length: 5 (Gomoku).
- Two players: Black moves first, White second, taking turns.
- Win condition: the first to form **n consecutive same-colored stones** in any horizontal, vertical, or diagonal direction wins.
- Draw: if the board fills up with no n-in-a-row achieved, the game is a draw.
- **Custom rules**: at startup the console prompts `Customize rules?`; type `c` to set the win length n (4..15) first, then the board size N (n..30); press Enter directly to use the default 15×15 Gomoku.

## Player Types

After startup, choose the player type for **Player 1** and **Player 2** in the console (Player 1 plays Black first, Player 2 plays White second):

| No. | Player | Description |
|:---:|:-------|:------------|
| 1 | Human | Place stones by mouse click |
| 2 | EasyJudge | Super easy: random moves + loss-prevention |
| 3 | PureGreed 1.0 | Defense-only scoring |
| 4 | PureGreed 1.1 | Attack+defense scoring |
| 5 | Minimax++ | Alpha-beta pruning + heuristic ordering + Zobrist transposition table (strongest) |
| 6 | API AI | Remote LLM API player (requires `config.json`) |

Any combination of the two sides is allowed; **AI vs. AI spectating is supported**. Non-numeric or out-of-range input defaults to the Human player.

> AI difficulty names are for relative distinction only and do not represent actual playing strength; please judge by experience.

## Memory Storage System

At startup, **only after typing `c` to enter the custom-rules flow** will the console ask `Enable memory storage? (y/n, default n)`. **Disabled by default**; game data is recorded only after entering `y`. If you press Enter directly to use the default rules, storage stays off.

### Feature Overview

| Feature | Description |
|:--------|:------------|
| Extended undo | Type `undo` or `undo 3` in-game to undo 1 or n moves |
| Game replay | Choose `3. Replay saved game` in the main menu to replay step by step (Space=next, B=prev, Home=first, End=last, ESC=exit) |
| Resume save | Type `save` in-game to save the current position as a resume anytime |
| Resume load | Choose `2. Load resume` in the main menu to restore a resume and continue |
| Abort anytime | Type `abort` in-game to abort the current game and auto-save it as a resume |
| Global statistics | Total moves, wins/losses/draws/aborts for both sides, accumulated across games |
| Auto-save | Automatically saves the current game as a resume when the program exits unexpectedly |

### Console Commands (type anytime in-game)

| Command | Short | Description |
|:--------|:-----:|:------------|
| `help` | `h` | Show all commands |
| `list` | `ls` | List all saved games |
| `list-r` | `lsr` | List all resumes |
| `replay <id>` | `rp` | Replay the specified game on the board |
| `save` | `s` | Save the current position as a resume |
| `undo [n]` | `u` | Undo n moves (default 1) |
| `abort` | `a` | Abort and save the current game |
| `stats` | `st` | Show global statistics |
| `status` | — | Show the current game status |

### Data Directory Structure

All data is written to the `data/` folder, with files named by **unique IDs** (machine-readable `key=value` text format; unknown fields are ignored automatically, supporting backward-compatible extension):

**File ID rule**: `prefix + YYYYMMDD_HHMMSS + sequence`
- `G` prefix = full game record (Game)
- `R` prefix = resume (Resume)

**File format example**:
```
# ChessGameRecord v1.0
id=G20260818_153000_001
boardSize=15
winLength=5
player1=Human
player2=Minimax++
status=BlackWin
startTime=1692345600000
endTime=1692345612345
blackMoves=30
whiteMoves=29
moveCount=59
[MOVES]
0,7,7,1,1692345600000
1,8,8,-1,1692345600500
[/MOVES]
```

## Class Design

| File | Class | Responsibility |
|:-----|:------|:---------------|
| `core.h/.cpp` | `Board` | Board data (1-D `std::vector`), holds size/winLen/emptyCount, place/read/O(1) fullness check |
| | `Judge` | Win/loss detection, reuses `scanLine` for unified line counting |
| | `scanLine` | Common inline line-scan kernel shared by Judge and all AIs |
| | `Stats` | Statistics (moves per side) |
| `ui.h/.cpp` | `UI` | **Encapsulates all EasyX calls**, holds layout parameters (gridSize/xOffset/yOffset/boardSize) |
| `player.h/.cpp` | `Player` | Abstract player base class |
| | `HumanPlayer` | Human, derived from Player |
| | `GreedyScoringAI` | **Generic scoring AI** with parameterized attack/defense weights; one implementation covers multiple difficulty tiers (0.0 defense-only / 0.9 attack+defense) |
| | `MinimaxPP` | Alpha-beta search AI, derived from Player |
| | `APIPlayer` | Remote LLM API AI, derived from Player |
| `ai_config.h/.cpp` | `AIConfig` | API AI config loading (JSON-driven) |
| `storage.h/.cpp` | `StorageManager` | **Game record storage management**: undo, replay, resumes, statistics, console commands |
| `controller.h/.cpp` | `GameController` | Main loop, rule configuration, terminal player selection, turn scheduling, storage integration |
| `main.cpp` | — | Only constructs the controller and calls `run()` |

### Class Relationships

- **Global mutable state eliminated**: board size/win length are held by `Board` members, layout parameters by `UI` members, supporting multiple coexisting boards and easy testing.
- `scanLine` (inline in core.h) is the unified line-counting kernel; `Judge::checkWin` and player.cpp's `inlineCheckN`/`pointScore`/`findOppCriticalThreats` share it, eliminating two independent line-detection implementations.
- `Player` is the abstract base class; all players **derive** from it with a unified `place()` interface.
- `HumanPlayer` holds a `UI&` reference for mouse input; `GreedyScoringAI` instantiates different difficulty tiers via constructor parameters (weights/name), so adding tiers requires no class changes.
- `GameController` **composes** `Board`/`Judge`/`Stats`/`StorageManager` (value semantics) plus `UI*` and two `Player*` (heap, released in destructor).
- `StorageManager` independently manages data persistence, configured via `StorageConfig`, with an extensible `key=value` file format.
- All EasyX-related calls are centralized in the `UI` class; replacing the graphics library in the future only requires changing this class.

### StorageManager Design (High Extensibility)

- **Interface segregation**: game lifecycle, undo, replay, resumes, statistics, and commands are independent interface groups
- **Configuration-driven**: `StorageConfig` controls enabling, directory, extensions, and all other behaviors
- **Extensible format**: `key=value` text format; unknown fields are ignored, adding fields never breaks older readers
- **Command pattern**: `handleConsoleCommand` can be extended with new commands
- **Status enums**: `GameStatus` is easy to extend with new states
- **Forward declarations**: reduce coupling; `storage.h` only forward-declares `Board`

## Directory Structure

```
chess/
├── core.h / core.cpp              Board, Judge, Stats, Pos, ChessType
├── ui.h / ui.cpp                  UI (EasyX wrapper)
├── player.h / player.cpp          Player base class + derived players (human/generic scoring AI/minimax/API)
├── ai_config.h / ai_config.cpp    AIConfig (API AI JSON config loading)
├── ai_player.h / ai_player.cpp    APIPlayer (remote LLM API player)
├── storage.h / storage.cpp        StorageManager (undo/replay/resumes/stats/commands)
├── controller.h / controller.cpp  GameController (rule config + turn scheduling + storage integration)
├── main.cpp                       Program entry
├── CMakeLists.txt                 CMake build script
├── cmake-local.cmake              Local EasyX path (ignored by .gitignore)
└── data/                          Data directory auto-created at runtime
    ├── stats.txt                  Global statistics
    ├── games/                     Game records
    └── resumes/                   Game resumes
```

## Build & Run

### Requirements

- OS: Windows
- Compiler: MSVC with C++17 support
- Build tool: CMake ≥ 3.15
- Third-party libraries: see Acknowledgements at the end

### Configuring the EasyX Path (Optional)

`CMakeLists.txt` looks for EasyX headers at these locations by default:

- `C:/Program Files (x86)/EasyX/include`
- `C:/EasyX/include`

If your EasyX is installed elsewhere, create `cmake-local.cmake` in the repository root (ignored by `.gitignore`, won't be committed):
If you see a warning like `libpng warning: iCCP: known incorrect sRGB profile`, it can be safely ignored and does not affect functionality.


```cmake
set(EASYX_LOCAL_PATH "D:/your/path/to/EasyX")
```

### Build Steps

```bash
cmake -B build
cmake --build build --config Release
```

The build output is `build/Release/Hello_chess.exe`.


## Quick Start

1. Run `Hello_chess.exe`
2. Configure rules: press Enter directly for the default 15×15 Gomoku, or type `c` to customize
3. Enable storage: only asked after typing `c` to customize rules; enter `y` to enable memory storage (or `n` to disable; pressing Enter for default rules keeps it off)
4. Choose the player type for both sides (1-6)
5. Start playing:
   - Human players place stones with mouse clicks
   - When storage is enabled, console commands can be typed anytime (`help` lists all commands)
6. After the game ends, choose to play again or return to the main menu
7. From the main menu you can replay historical games or load resumes to continue

## Minimax++ Algorithm Details (Strongest AI)

Minimax++ uses Minimax search with Alpha-Beta pruning, plus multiple performance and correctness optimizations:

### Search Framework
- **Alpha-Beta pruning**: depth-first search, pruning branches that cannot affect the decision.
- **Search depth 4** (adjustable via `kDepth` at the top of `player.h`); with the optimizations below it completes in reasonable time.
- **Win-distance weighting**: shallower wins score higher, pushing the AI to prefer the fastest win / slowest loss paths.

### Performance Optimizations
| Technique | Description |
|:----------|:------------|
| Heuristic ordering | Candidate moves are sorted descending by `pointScore` before searching; high-score nodes are searched first, improving pruning efficiency by 10-100x |
| Zobrist transposition table | 64-bit hashes identify positions; `unordered_map` stores searched results to avoid recomputing identical positions |
| Static buffers | `generateMoves` uses static boolean arrays for dedup, avoiding heap allocation inside recursion |
| Unified score table | `evaluate` and `pointScore` share the `segValue` scoring kernel, eliminating dimensional inconsistency |
| Unified `scanLine` kernel | `Judge::checkWin`/`inlineCheckN`/`pointScore`/`findOppCriticalThreats` share line counting, single-point maintenance |
| Neighborhood scanning | Threat detection/winning moves limited to the neighborhood of existing stones (radius=2), replacing full-board O(N²) scans |
| O(1) fullness check | `Board` maintains `emptyCount_`; `isFull()` runs in constant time |

### Correctness Guarantees
- **Winning-move takeover**: detects 1-step / 2-step forced wins first; plays immediately on hit.
- **Opponent one-to-win**: detects positions where the opponent would complete a line next move and blocks immediately.
- **Defensive candidates take the union**: open-three/open-four threats (`critical`) and closed-four/four threats (`must`) are **unioned** rather than mutually exclusive, fixing the fatal legacy bug of "blocking only the open three while a closed four completes five".
- **Geometric score scaling**: open-four ≫ open-three ≫ open-two; priorities are strictly monotonic for any win length (>=4).

### Decision Flow
```
place():
  1. Winning moves (1-step/2-step win) → return on hit
  2. Opponent one-step completion → return the blocking point on hit
  3. Merged defense candidates = critical ∪ must (deduplicated)
  4. Candidates = defense candidates if non-empty, else all empty neighborhood cells
  5. Run minimax (depth 3) on each candidate + heuristic tie-breaking, pick the best
```

---

## API AI Player (Remote LLM)

Player 6, the API AI, places stones by calling a remote large language model via HTTP API, supporting any OpenAI-compatible endpoint. **If unconfigured or misconfigured, choosing 6 automatically falls back to Minimax++ (player 5)** with the reason printed to the console; the program never crashes.

### Configuration

`config.json` is a **ready-to-use template**: prompts, temperature, etc. are pre-configured; **just fill in `api_url`, `api_key`, `model`** (`display_name` falls back to the model name when empty). No provider address is pre-filled, and none is endorsed. `config.example.json` provides a complete OpenAI-based example for reference.


| Field | Description |
|:------|:------------|
| `display_name` | Model name shown in the console (falls back to `model` when empty) |
| `api_url` | API endpoint (OpenAI-compatible format supported), **required, not pre-filled**; choose per the list below |
| `api_key` | API key, required (left empty in the template, fill in yourself) |
| `model` | Model name, required; switching models only changes this line |
| `temperature` | Generation temperature (0.0-2.0) |
| `max_tokens` | Maximum output tokens |
| `system_prompt` | System prompt, supports `{size}` `{win_len}` `{color}` variables |
| `user_prompt_template` | User prompt template, supports `{board}` `{color}` variables |

> **Note**: `config.json` contains the API key and is ignored by `.gitignore`; it won't be committed to the repository. `config.example.json` contains no sensitive information and is committed for format reference only.


## Acknowledgements

This project owes its development to the following open-source libraries:

- **EasyX** graphics library: all graphics rendering and interaction in this project are built upon it.
- **libcurl**: network request implementation for the API AI player.
- **nlohmann/json**: config parsing and JSON processing of API responses.

- EasyX official site: <https://easyx.cn>
- libcurl official site: <https://curl.se>
- nlohmann/json: <https://github.com/nlohmann/json>