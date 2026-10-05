// ============================================================
// app/JsonProtocol.cpp - 转换实现
// ============================================================
#include "JsonProtocol.h"

namespace gomoku {

nlohmann::json toJson(const ViewState& s) {
    nlohmann::json j;
    j["status"] = s.status;
    j["matchId"] = s.matchId;
    j["boardSize"] = s.boardSize;
    j["winLength"] = s.winLength;
    j["cells"] = s.cells;                 // 一维数组，r*boardSize+c
    j["turn"] = static_cast<int>(s.turn);  // 1 黑 / -1 白
    j["moveCount"] = s.moveCount;
    j["lastMove"] = { s.lastMove.r, s.lastMove.c };
    j["lastBlack"] = { s.lastBlack.r, s.lastBlack.c };
    j["lastWhite"] = { s.lastWhite.r, s.lastWhite.c };

    j["players"] = {
        { "black", { { "id", s.blackId }, { "name", s.blackName } } },
        { "white", { { "id", s.whiteId }, { "name", s.whiteName } } },
    };

    j["thinking"] = s.thinking;
    j["humanTurn"] = s.humanTurn;
    j["canUndo"] = s.canUndo;
    j["storageEnabled"] = s.storageEnabled;
    j["version"] = s.version;
    j["message"] = s.message;
    return j;
}

nlohmann::json playerCatalogJson(const std::vector<PlayerInfo>& catalog) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : catalog) {
        arr.push_back({
            { "id", p.id },
            { "name", p.display },
            { "description", p.description },
            { "human", p.human },
            { "ready", p.ready },
            { "fallbackId", p.fallbackId },
        });
    }
    return arr;
}

NewGameRequest parseNewGame(const nlohmann::json& body) {
    NewGameRequest req;
    if (!body.is_object()) return req;

    req.rules.boardSize = body.value("boardSize", 15);
    req.rules.winLength = body.value("winLength", 5);
    req.rules = req.rules.normalized();

    req.black = body.value("black", std::string("human"));
    req.white = body.value("white", std::string("human"));
    req.storageEnabled = body.value("storageEnabled", true);
    return req;
}

nlohmann::json okJson() {
    return nlohmann::json{ { "ok", true } };
}

nlohmann::json errJson(const std::string& what) {
    return nlohmann::json{ { "ok", false }, { "error", what } };
}

nlohmann::json storedListJson(const std::vector<StoredBrief>& items) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& it : items) {
        arr.push_back({
            { "id", it.id },
            { "boardSize", it.boardSize },
            { "winLength", it.winLength },
            { "black", it.black },
            { "white", it.white },
            { "moves", it.moves },
            { "note", it.note },
        });
    }
    return arr;
}

}  // namespace gomoku
