// ============================================================
// storage.cpp - 棋局存储管理类实现
// 文件格式：key=value 文本，机器可读，未知字段忽略（可扩展）
// ============================================================
#include "storage.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <conio.h>
#include <direct.h>
#include <io.h>
#include <cstdio>

StorageManager::StorageManager() {
    loadGlobalStats();
}

StorageManager::~StorageManager() {
    if (inGame_) {
        saveResume("Auto-saved on exit");
        endGame(GameStatus::Aborted);
    }
    saveGlobalStats();
}

// ====== 配置 ======
void StorageManager::setEnabled(bool enabled) { config_.enabled = enabled; }
bool StorageManager::isEnabled() const { return config_.enabled; }
void StorageManager::setConfig(const StorageConfig& cfg) { config_ = cfg; }
const StorageConfig& StorageManager::config() const { return config_; }

// ====== 棋局生命周期 ======
void StorageManager::startGame(int boardSize, int winLength,
                               const std::string& p1, const std::string& p2,
                               int p1Type, int p2Type) {
    if (!config_.enabled) return;
    current_ = GameRecord{};
    current_.id = generateId("G");
    current_.boardSize = boardSize;
    current_.winLength = winLength;
    current_.player1Name = p1;
    current_.player2Name = p2;
    current_.player1Type = p1Type;
    current_.player2Type = p2Type;
    current_.status = GameStatus::InProgress;
    current_.startTime = nowMs();
    inGame_ = true;
    abortRequested_ = false;
}

void StorageManager::recordMove(int r, int c, ChessType color) {
    if (!config_.enabled || !inGame_) return;
    MoveRecord mv;
    mv.step = (int)current_.moves.size();
    mv.r = r;
    mv.c = c;
    mv.color = color;
    mv.timestamp = nowMs();
    current_.moves.push_back(mv);
    if (color == ChessType::Black)      current_.blackMoves++;
    else if (color == ChessType::White) current_.whiteMoves++;
}

void StorageManager::endGame(GameStatus status) {
    if (!config_.enabled || !inGame_) return;
    current_.status = status;
    current_.endTime = nowMs();
    saveRecord(current_, config_.gamesDir);
    updateGlobalStats(current_);
    saveGlobalStats();
    inGame_ = false;
}

bool StorageManager::isInGame() const { return inGame_; }
const std::string& StorageManager::currentGameId() const { return current_.id; }
const GameRecord& StorageManager::currentRecord() const { return current_; }

// ====== 悔棋 ======
bool StorageManager::canUndo() const {
    return inGame_ && !current_.moves.empty();
}

bool StorageManager::undoLastMove(Board& board) {
    if (!canUndo()) return false;
    MoveRecord last = current_.moves.back();
    board.set(last.r, last.c, ChessType::None);
    current_.moves.pop_back();
    if (last.color == ChessType::Black)      current_.blackMoves--;
    else if (last.color == ChessType::White) current_.whiteMoves--;
    return true;
}

int StorageManager::undoMoves(Board& board, int n) {
    int undone = 0;
    for (int i = 0; i < n && canUndo(); i++) {
        if (undoLastMove(board)) undone++;
    }
    return undone;
}

int StorageManager::moveCount() const {
    return (int)current_.moves.size();
}

// ====== 棋局回访 ======
std::vector<std::string> StorageManager::listGames() const {
    std::vector<std::string> ids;
    std::string dir = dirPath(config_.gamesDir);
    ensureDir(dir);

    _finddata_t fd;
    std::string pattern = dir + "\\*" + config_.fileExt;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return ids;
    do {
        if (!(fd.attrib & _A_SUBDIR)) {
            std::string name = fd.name;
            size_t pos = name.rfind(config_.fileExt);
            if (pos != std::string::npos) name = name.substr(0, pos);
            ids.push_back(name);
        }
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<GameRecord> StorageManager::listGameRecords() const {
    std::vector<GameRecord> records;
    for (const auto& id : listGames()) {
        GameRecord r;
        if (loadGame(id, r)) records.push_back(r);
    }
    return records;
}

bool StorageManager::loadGame(const std::string& id, GameRecord& record) const {
    std::string path = filePath(config_.gamesDir, id + config_.fileExt);
    return loadRecord(path, record);
}

bool StorageManager::replayGame(const std::string& id, Board& board, int upToStep) const {
    GameRecord record;
    if (!loadGame(id, record)) return false;

    board.clear();
    int count = (upToStep < 0) ? (int)record.moves.size()
                               : std::min(upToStep, (int)record.moves.size());
    for (int i = 0; i < count; i++) {
        const auto& mv = record.moves[i];
        board.set(mv.r, mv.c, mv.color);
    }
    return true;
}

// ====== 残局保存/载入 ======
std::vector<std::string> StorageManager::listResumes() const {
    std::vector<std::string> ids;
    std::string dir = dirPath(config_.resumesDir);
    ensureDir(dir);

    _finddata_t fd;
    std::string pattern = dir + "\\*" + config_.fileExt;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return ids;
    do {
        if (!(fd.attrib & _A_SUBDIR)) {
            std::string name = fd.name;
            size_t pos = name.rfind(config_.fileExt);
            if (pos != std::string::npos) name = name.substr(0, pos);
            ids.push_back(name);
        }
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool StorageManager::saveResume(const std::string& note) {
    if (!config_.enabled || !inGame_) return false;

    GameRecord resume = current_;
    resume.id = generateId("R");
    resume.status = GameStatus::Aborted;
    resume.endTime = nowMs();
    resume.note = note;

    return saveRecord(resume, config_.resumesDir);
}

bool StorageManager::loadResume(const std::string& id, GameRecord& record) const {
    std::string path = filePath(config_.resumesDir, id + config_.fileExt);
    return loadRecord(path, record);
}

bool StorageManager::restoreResume(const std::string& id, Board& board,
                                   int& boardSize, int& winLength,
                                   int& p1Type, int& p2Type) {
    GameRecord record;
    if (!loadResume(id, record)) return false;

    boardSize = record.boardSize;
    winLength = record.winLength;
    p1Type = record.player1Type;
    p2Type = record.player2Type;
    ROWS = COLS = boardSize;
    WIN_LEN = winLength;
    board.resize();
    board.clear();

    for (const auto& mv : record.moves) {
        board.set(mv.r, mv.c, mv.color);
    }

    current_ = record;
    current_.id = generateId("G");
    current_.status = GameStatus::Resumed;
    current_.startTime = nowMs();
    current_.endTime = 0;
    current_.note = "Resumed from " + id;
    inGame_ = true;
    abortRequested_ = false;
    return true;
}

// ====== 全局统计 ======
GlobalStats StorageManager::globalStats() const { return globalStats_; }

void StorageManager::printGlobalStats() const {
    printf("=== Global Statistics ===\n");
    printf("  Total games:       %d\n", globalStats_.totalGames);
    printf("  Black wins:        %d\n", globalStats_.blackWins);
    printf("  White wins:        %d\n", globalStats_.whiteWins);
    printf("  Draws:             %d\n", globalStats_.draws);
    printf("  Aborts:            %d\n", globalStats_.aborts);
    printf("  Black total moves: %d\n", globalStats_.blackTotalMoves);
    printf("  White total moves: %d\n", globalStats_.whiteTotalMoves);
    printf("=========================\n");
}

// ====== 控制台命令 ======
bool StorageManager::handleConsoleCommand(const std::string& cmd, Board& board) {
    if (cmd.empty()) return false;

    std::istringstream iss(cmd);
    std::string action;
    iss >> action;

    if (action == "help" || action == "h") {
        printHelp();
        return true;
    }
    if (action == "list" || action == "ls") {
        auto ids = listGames();
        printf("=== Saved Games (%d) ===\n", (int)ids.size());
        for (const auto& id : ids) {
            GameRecord r;
            if (loadGame(id, r)) {
                printf("  %s | %dx%d | %s vs %s | %s | moves=%d\n",
                       id.c_str(), r.boardSize, r.winLength,
                       r.player1Name.c_str(), r.player2Name.c_str(),
                       statusToString(r.status).c_str(), (int)r.moves.size());
            }
        }
        return true;
    }
    if (action == "list-r" || action == "lsr") {
        auto ids = listResumes();
        printf("=== Saved Resumes (%d) ===\n", (int)ids.size());
        for (const auto& id : ids) {
            GameRecord r;
            if (loadResume(id, r)) {
                printf("  %s | %dx%d | moves=%d | note=%s\n",
                       id.c_str(), r.boardSize, r.winLength,
                       (int)r.moves.size(), r.note.c_str());
            }
        }
        return true;
    }
    if (action == "replay" || action == "rp") {
        std::string id;
        iss >> id;
        if (id.empty()) {
            printf("Usage: replay <id>\n");
            return true;
        }
        GameRecord r;
        if (!loadGame(id, r)) {
            printf("Game not found: %s\n", id.c_str());
            return true;
        }
        printf("Replaying %s: %dx%d, %s vs %s, %d moves\n",
               id.c_str(), r.boardSize, r.winLength,
               r.player1Name.c_str(), r.player2Name.c_str(),
               (int)r.moves.size());
        board.clear();
        for (const auto& mv : r.moves) {
            board.set(mv.r, mv.c, mv.color);
        }
        printf("Replayed %d moves. Check the board.\n", (int)r.moves.size());
        return true;
    }
    if (action == "save" || action == "s") {
        if (saveResume("Manual save")) {
            printf("Resume saved.\n");
        } else {
            printf("Failed to save resume.\n");
        }
        return true;
    }
    if (action == "undo" || action == "u") {
        int n = 1;
        iss >> n;
        if (n < 1) n = 1;
        int undone = undoMoves(board, n);
        printf("Undone %d moves.\n", undone);
        return undone > 0;
    }
    if (action == "abort" || action == "a") {
        saveResume("Aborted by user");
        abortRequested_ = true;
        printf("Game aborted. Resume saved.\n");
        return true;
    }
    if (action == "stats" || action == "st") {
        printGlobalStats();
        return true;
    }
    if (action == "status") {
        if (inGame_) {
            printf("Current game: %s | moves=%d | black=%d white=%d\n",
                   current_.id.c_str(), (int)current_.moves.size(),
                   current_.blackMoves, current_.whiteMoves);
        } else {
            printf("No active game.\n");
        }
        return true;
    }

    printf("Unknown command: %s (type 'help' for help)\n", action.c_str());
    return false;
}

void StorageManager::printHelp() const {
    printf("=== Storage Commands ===\n");
    printf("  help / h        Show this help\n");
    printf("  list / ls       List all saved games\n");
    printf("  list-r / lsr    List all saved resumes\n");
    printf("  replay <id>     Replay a game on board\n");
    printf("  save / s        Save current as resume\n");
    printf("  undo [n] / u    Undo n moves (default 1)\n");
    printf("  abort / a       Abort and save current game\n");
    printf("  stats / st      Show global statistics\n");
    printf("  status          Show current game status\n");
    printf("========================\n");
}

bool StorageManager::isAbortRequested() const { return abortRequested_; }
void StorageManager::clearAbortRequest() { abortRequested_ = false; }

// ====== 非阻塞控制台输入 ======
std::string StorageManager::tryReadConsoleLine() {
    static std::string buffer;
    while (_kbhit()) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') {
            std::string line = buffer;
            buffer.clear();
            printf("\n");
            return line;
        }
        if (ch == 8 || ch == 127) {
            if (!buffer.empty()) {
                buffer.pop_back();
                printf("\b \b");
            }
        } else if (ch >= 32 && ch < 127) {
            buffer.push_back((char)ch);
            printf("%c", ch);
        }
    }
    return "";
}

// ====== 私有工具 ======
std::string StorageManager::generateId(const std::string& prefix) const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);

    static int seq = 0;
    seq = (seq + 1) % 1000;

    std::ostringstream oss;
    oss << prefix << buf << "_" << std::setfill('0') << std::setw(3) << seq;
    return oss.str();
}

std::string StorageManager::dirPath(const std::string& subdir) const {
    if (subdir.empty()) return config_.dataDir;
    return config_.dataDir + "\\" + subdir;
}

std::string StorageManager::filePath(const std::string& subdir, const std::string& filename) const {
    return dirPath(subdir) + "\\" + filename;
}

void StorageManager::ensureDir(const std::string& dir) const {
    std::string current;
    for (size_t i = 0; i < dir.size(); i++) {
        current += dir[i];
        if (dir[i] == '\\' || i == dir.size() - 1) {
            _mkdir(current.c_str());
        }
    }
}

int64_t StorageManager::nowMs() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    return (int64_t)ms.count();
}

bool StorageManager::saveRecord(const GameRecord& record, const std::string& subdir) const {
    std::string dir = dirPath(subdir);
    ensureDir(dir);
    std::string path = filePath(subdir, record.id + config_.fileExt);

    std::ofstream ofs(path);
    if (!ofs) return false;

    ofs << "# ChessGameRecord v1.0\n";
    ofs << "id=" << record.id << "\n";
    ofs << "boardSize=" << record.boardSize << "\n";
    ofs << "winLength=" << record.winLength << "\n";
    ofs << "player1=" << record.player1Name << "\n";
    ofs << "player2=" << record.player2Name << "\n";
    ofs << "player1Type=" << record.player1Type << "\n";
    ofs << "player2Type=" << record.player2Type << "\n";
    ofs << "status=" << statusToString(record.status) << "\n";
    ofs << "startTime=" << record.startTime << "\n";
    ofs << "endTime=" << record.endTime << "\n";
    ofs << "blackMoves=" << record.blackMoves << "\n";
    ofs << "whiteMoves=" << record.whiteMoves << "\n";
    ofs << "note=" << record.note << "\n";
    ofs << "moveCount=" << record.moves.size() << "\n";
    ofs << "[MOVES]\n";
    for (const auto& mv : record.moves) {
        ofs << mv.step << "," << mv.r << "," << mv.c << ","
            << static_cast<int>(mv.color) << "," << mv.timestamp << "\n";
    }
    ofs << "[/MOVES]\n";
    return true;
}

bool StorageManager::loadRecord(const std::string& filepath, GameRecord& record) const {
    std::ifstream ifs(filepath);
    if (!ifs) return false;

    record = GameRecord{};
    std::string line;
    bool inMoves = false;

    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line == "[MOVES]") { inMoves = true; continue; }
        if (line == "[/MOVES]") { inMoves = false; continue; }

        if (inMoves) {
            std::istringstream iss(line);
            MoveRecord mv;
            char sep;
            int colorInt;
            iss >> mv.step >> sep >> mv.r >> sep >> mv.c >> sep >> colorInt >> sep >> mv.timestamp;
            mv.color = static_cast<ChessType>(colorInt);
            record.moves.push_back(mv);
            continue;
        }

        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        if (key == "id")            record.id = val;
        else if (key == "boardSize")   record.boardSize = std::stoi(val);
        else if (key == "winLength")   record.winLength = std::stoi(val);
        else if (key == "player1")     record.player1Name = val;
        else if (key == "player2")     record.player2Name = val;
        else if (key == "player1Type") record.player1Type = std::stoi(val);
        else if (key == "player2Type") record.player2Type = std::stoi(val);
        else if (key == "status")      record.status = stringToStatus(val);
        else if (key == "startTime")   record.startTime = std::stoll(val);
        else if (key == "endTime")     record.endTime = std::stoll(val);
        else if (key == "blackMoves")  record.blackMoves = std::stoi(val);
        else if (key == "whiteMoves")  record.whiteMoves = std::stoi(val);
        else if (key == "note")        record.note = val;
    }
    return true;
}

void StorageManager::loadGlobalStats() {
    std::string path = filePath("", config_.statsFile);
    std::ifstream ifs(path);
    if (!ifs) return;

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        if (key == "totalGames")       globalStats_.totalGames = std::stoi(val);
        else if (key == "blackWins")      globalStats_.blackWins = std::stoi(val);
        else if (key == "whiteWins")      globalStats_.whiteWins = std::stoi(val);
        else if (key == "draws")          globalStats_.draws = std::stoi(val);
        else if (key == "aborts")         globalStats_.aborts = std::stoi(val);
        else if (key == "blackTotalMoves") globalStats_.blackTotalMoves = std::stoi(val);
        else if (key == "whiteTotalMoves") globalStats_.whiteTotalMoves = std::stoi(val);
        else if (key == "lastUpdate")     globalStats_.lastUpdate = std::stoll(val);
    }
}

void StorageManager::saveGlobalStats() const {
    std::string dir = dirPath("");
    ensureDir(dir);
    std::string path = filePath("", config_.statsFile);

    std::ofstream ofs(path);
    if (!ofs) return;

    ofs << "# ChessGlobalStats v1.0\n";
    ofs << "totalGames=" << globalStats_.totalGames << "\n";
    ofs << "blackWins=" << globalStats_.blackWins << "\n";
    ofs << "whiteWins=" << globalStats_.whiteWins << "\n";
    ofs << "draws=" << globalStats_.draws << "\n";
    ofs << "aborts=" << globalStats_.aborts << "\n";
    ofs << "blackTotalMoves=" << globalStats_.blackTotalMoves << "\n";
    ofs << "whiteTotalMoves=" << globalStats_.whiteTotalMoves << "\n";
    ofs << "lastUpdate=" << globalStats_.lastUpdate << "\n";
}

void StorageManager::updateGlobalStats(const GameRecord& record) {
    globalStats_.totalGames++;
    if (record.status == GameStatus::BlackWin)      globalStats_.blackWins++;
    else if (record.status == GameStatus::WhiteWin) globalStats_.whiteWins++;
    else if (record.status == GameStatus::Draw)     globalStats_.draws++;
    else if (record.status == GameStatus::Aborted)  globalStats_.aborts++;
    globalStats_.blackTotalMoves += record.blackMoves;
    globalStats_.whiteTotalMoves += record.whiteMoves;
    globalStats_.lastUpdate = nowMs();
}

std::string StorageManager::statusToString(GameStatus s) const {
    switch (s) {
        case GameStatus::InProgress: return "InProgress";
        case GameStatus::BlackWin:   return "BlackWin";
        case GameStatus::WhiteWin:   return "WhiteWin";
        case GameStatus::Draw:       return "Draw";
        case GameStatus::Aborted:    return "Aborted";
        case GameStatus::Resumed:    return "Resumed";
        default: return "Unknown";
    }
}

GameStatus StorageManager::stringToStatus(const std::string& s) const {
    if (s == "InProgress") return GameStatus::InProgress;
    if (s == "BlackWin")   return GameStatus::BlackWin;
    if (s == "WhiteWin")   return GameStatus::WhiteWin;
    if (s == "Draw")       return GameStatus::Draw;
    if (s == "Aborted")    return GameStatus::Aborted;
    if (s == "Resumed")    return GameStatus::Resumed;
    return GameStatus::InProgress;
}