# Gomoku

A Gomoku (five-in-a-row) desktop game in C++17, built on a microkernel: the kernel knows the
rules of the board and nothing else. Players, frontends, storage and launch modes are plugins.

The interesting part of this project is not the game. It is the dependency structure that lets
each of those four axes be replaced without touching the other three.

---

## Design route

The starting point was a conventional monolith: one translation unit that mixed board rules,
AI search, console I/O, storage and window handling. Every new requirement — a second frontend,
a second AI, a replay mode — cut across all of it.

The rewrite takes the opposite route:

1. **Find the axis that must not change, and shrink it.** What a Gomoku board *is* (placements,
   win detection, turn order, draw) is stable. Everything else is a policy decision. The kernel
   keeps only the former.
2. **Make the boundary a data structure, not a call.** Frontends never receive a `Board&`. They
   receive an immutable `ViewState` snapshot; they send commands through `IControllable`. There
   is no path by which a view can mutate the game.
3. **Make extension a one-line registration.** Adding a player or a frontend is one class plus one
   `reg.add(...)` line; neither `kernel/` nor `contracts/` is edited.
4. **Put the legacy world behind an adapter.** The existing search engines are several thousand
   lines of tuned, working code. They are kept in `engine/` and adapted to the plugin contract
   rather than rewritten.

The dependency rule is enforced by inspection: `contracts/` may include only the standard library.
`SelfTestHost` scans those headers at runtime and fails the check if an upper layer sneaks in.

---

## Layer map

```
contracts/   pure interfaces and value types; zero implementation dependencies
    ↑
kernel/      board, rules, match state machine, session orchestration, storage boundary
    ↑
app/         JSON protocol, option parsing, view driver loop
    ↑
plugins/     players · views · storage
    ↑
hosts/       cli · web · selfplay · selftest
    ↑
main_microkernel.cpp   parse arguments, pick one host
```

| Layer | Owns | Replacing it means |
|---|---|---|
| `contracts/` | `IPlayer`, `IView`, `IControllable`, `IHost`, `IBoard`, `IRules`, `IStorageGateway` | changing the vocabulary of every layer |
| `kernel/` | `SquareBoard`, `GomokuRules`, `MatchState`, `MatchSession` | changing the board geometry or the ruleset |
| `app/` | `JsonProtocol`, `Options`, `ViewDriver`, `ResourcePaths` | changing the wire format or the input loop |
| `plugins/` | concrete players, frontends, storage | adding or swapping capabilities |
| `hosts/` | launch modes | changing how the process runs |

---

## The four contracts

**`IPlayer`** — one method: `Decision tick(const ViewState&, const ThinkBudget&)`. `human()`
returns true for a seat controlled by the frontend; the kernel then never calls `tick()` and
accepts moves through `IControllable::play()` instead. `ThinkBudget` is a hard expectation, not
a hint: the kernel calls `tick()` on the control thread, so an engine that ignores its budget
makes the UI feel stuck.

**`IView`** — `render(const ViewState&)` plus `poll()`. `render` runs on the kernel's control
thread and must never block; `poll` runs on the view driver's thread and is where input is
consumed. Views are observers, plural by design: a web UI and a console log can watch the same
match.

**`IControllable`** — the entire command surface (`startGame`, `play`, `undo`, `abort`,
`swapPlayer`, `saveResume`, `loadResume`, `replay`, `requestQuit`, ...). All of it is
thread-safe and serialised by one mutex; callers enqueue, the control thread executes.

**`IHost`** — `onRequest` (method, path, body → reply) and `onStream` (SSE). The HTTP library
and the SSE transport live entirely behind this interface; swapping `cpp-httplib` for something
else touches one file.

---

## Data flow

```
            ┌──────────────── control thread ────────────────┐
  commands → │ queue → IControllable → MatchState → snapshot │ → ViewState → JSON → SSE
            └────────────────────────────────────────────────┘
```

One direction only. The frontend receives snapshots and emits commands; it has no reference to
the board, the rules, or the storage layer.

The JSON frame is the only cross-language surface, which is why it lives in a single file
(`app/JsonProtocol.cpp`) and why every untrusted field goes through one tolerant accessor
(`jsonValueOr`) instead of `nlohmann::json::value<T>()` — the latter throws on type mismatch, and
that exception would otherwise escape a worker thread and terminate the process.

---

## Threading model

Deliberately small:

- **One control thread** owns the board. All state mutation happens there, serialised.
- **Caller threads** (HTTP handlers, the view driver) do exactly two things: enqueue a command,
  or read a snapshot. Neither touches game state directly.
- **AI thinking** happens on the control thread, but only after releasing the state lock and
  pushing a "thinking" frame. Commands that arrive meanwhile are queued, not executed; when the
  move returns, a generation counter (`reqGen_`) discards the result if anything invalidated it.
- **Observer callbacks run outside the lock.** A slow frontend can no longer stall the match.
  Frames are accumulated under the lock and dispatched after unlocking.

Two consequences worth naming, because they shaped the code:

1. Moving callbacks out of the lock turned a deadlock into a lifetime problem. The pending frame
   holds raw `IView*` targets, so `removeObserver` clears them from the in-flight frame as well,
   and hosts shut down in a fixed order (`stop driver → remove observer → request quit → join
   until the thread is really finished → destroy the view`). Both halves are required: clearing
   the queue covers frames not yet dispatched; the join order covers the frame already being
   rendered outside the lock.
2. Because AI runs on the control thread, the think budget is a usability feature, not a
   performance tweak. Search engines honour it by checking a clock every N nodes and refusing to
   write a truncated result into the transposition table.

---

## Size limits are enforced three times, on purpose

The legal range of a match is `boardSize ∈ [4, 30]`, `winLength ∈ [4, 15]`. The upper bounds are
not arbitrary: the search engines use fixed arrays (`std::array<int, 15>` for a window,
`zobrist_[30][30][2]` for hashing). An out-of-range value is a memory-safety bug, not a rules bug.

The same limit is therefore checked at three depths:

1. `RulesConfig::normalized()` at the contract boundary — every entry point (HTTP new-game,
   command line, resume loading) funnels through it;
2. `SquareBoard::reset` / `Board::resize` — the board clamps itself, so a future caller that
   forgets to normalize cannot overflow `n * n`;
3. `TacticalMax::buildIndex` — the engine validates before its first allocation, because two
   entry points (legacy resume restore and the benchmark harness) bypass the kernel entirely.

Defence in depth is deliberate here. The first check is the design; the other two exist because
"every caller remembers to normalize" is not a property a type system enforces.

---

## The legacy engine island

`engine/` is the pre-microkernel AI world: minimax with alpha-beta and a transposition table, an
incremental-evaluation tactical engine, a threat detector, and the original scoring core. It is
kept because it works and it is tuned.

`plugins/players/EnginePlayers.cpp` is the adapter. It converts a `ViewState` snapshot into the
mutable `Board` those engines expect, calls `tick`, and converts the result back into a
`Decision`. The kernel never sees `engine/`; deleting the directory and the adapter would leave a
working product with no built-in AI.

This is the one place where the architecture is honestly a compromise: there are two board
representations (kernel `SquareBoard` with `Stone`, legacy `Board` with `ChessType`) kept in sync
by integer-compatible enum values. It is contained to the adapter, but it is real.

---

## Building

Requires CMake 3.15+, a C++17 compiler, and `nlohmann_json` + `libcurl` (vcpkg is used here).
EasyX is optional and only needed for the native-window frontend.

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
```

Outputs `Gomoku.exe` and `GomokuBench.exe`, with `webapp/` and the runtime DLLs copied next to
them. For step-by-step instructions, frontend usage and troubleshooting, see the Chinese
companion document, `README.zh-CN.md`.

---

## Known limits

- The default frontend is the browser UI. The native-window frontend requires EasyX, which is an
  optional dependency and is not bundled.
- AI moves are computed on the control thread. The budget keeps this bounded (800 ms by default),
  but a move is not preemptive: a command issued mid-think waits for that move to finish.
- The HTTP host binds to `127.0.0.1` only and validates `Host` / `Origin`. It is a local UI, not a
  server.
- Storage is a line-oriented text format. It tolerates corruption and sanitises identifiers, but
  it is not a database.
