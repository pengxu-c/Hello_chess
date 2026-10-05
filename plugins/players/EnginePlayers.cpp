// ============================================================
// plugins/players/EnginePlayers.cpp - 适配器实现 + 内置本地引擎绑定
//
// 本文件是「旧世界」与「新内核」之间唯一的接触面。
// 旧引擎需要 Board&/ChessType/Judge，新内核只给只读 ViewState —— 转换全在这里，
// 因此将来要彻底替换掉旧引擎时，删掉本文件再注册新玩家即可，内核完全无感。
//
// 刻意不包含 ai_player.h（网络玩家由 plugins/PlayerRegistry.cpp 单独注册），
// 这样 bench 目标链接本文件时不会被迫拖进 libcurl。
// ============================================================
#include "EnginePlayers.h"

#include "../../engine/core.h"          // 旧引擎的棋盘/裁判
#include "../../engine/player.h"        // legacy::Player 基类与各档位
#include "../../engine/tactical_max.h"  // TacticalMax

#include <functional>
#include <memory>

namespace gomoku {
namespace {

// ---- 契约层 Stone ↔ 旧引擎 ChessType 的双向映射 ----
inline ChessType toLegacy(Stone s) { return static_cast<ChessType>(static_cast<int>(s)); }
inline Stone fromLegacy(ChessType c) { return static_cast<Stone>(static_cast<int>(c)); }

// ============================================================
// EngineHandle - 任意旧引擎的通用外壳
//
// 关键点：每步都从 history 重建棋盘，因此
//   * 引擎自己搜索时对棋盘的临时改动不会泄漏到下一步；
//   * 内核换棋盘实现（六边形等）时，本文件是唯一需要改的地方。
// ============================================================
class EngineHandle final : public IEngineHandle {
public:
    EngineHandle(std::string name, std::unique_ptr<Player> engine)
        : name_(std::move(name)), engine_(std::move(engine)) {}

    std::string name() const override { return name_; }

    // 暴露给 setBudget 回调：按具体类型设置思考预算（有就设，没有就算了）
    Player* raw() const { return engine_.get(); }

    Coord decide(int boardSize, int winLength, const std::vector<Move>& history,
                 Stone color) override {
        // 1) 按需重建旧棋盘（尺寸或步数变化时）
        if (board_.size() != boardSize) {
            board_.resize(boardSize);
            builtMoves_ = -1;
        }
        board_.setWinLen(winLength);
        if (builtMoves_ != static_cast<int>(history.size())) {
            board_.clear();
            for (const auto& m : history) board_.place(m.r, m.c, toLegacy(m.color));
            builtMoves_ = static_cast<int>(history.size());
        }

        // 2) 交给旧引擎决策（它会自行搜索/思考，可能修改 board_ 的临时状态）
        const Pos p = engine_->place(board_, toLegacy(color));
        builtMoves_ = -1;   // 引擎动过棋盘 → 下一步必须重建

        if (!p.valid()) return Coord{ -1, -1 };
        return Coord{ p.r, p.c };
    }

private:
    std::string name_;
    std::unique_ptr<Player> engine_;
    Board board_;        // 旧棋盘缓存
    int builtMoves_ = -1;  // 已按多少手重建过（-1 表示需重建）
};

// ============================================================
// API 玩家已移出本文件
//   它需要 libcurl 与 config.json，属于"有外部依赖的插件"，
//   因此注册逻辑放在 plugins/PlayerRegistry.cpp，由那个文件独自承担网络依赖。
//   这样 bench（只跑本地算法）不必链接 libcurl。
// ============================================================

// ---- 各档位的构造器：唯一需要认识具体引擎类的地方 ----
std::shared_ptr<IEngineHandle> makeEasy() {
    return std::make_shared<EngineHandle>("EasyJudge", std::make_unique<EasyJudgeAI>());
}

std::shared_ptr<IEngineHandle> makePureGreed(double attack, const char* name) {
    return std::make_shared<EngineHandle>(name,
                                          std::make_unique<GreedyScoringAI>(attack, name));
}

std::shared_ptr<IEngineHandle> makeMinimax() {
    // MinimaxPP 需要一个裁判实例；裁判是无状态的，这里自持一个即可
    // （这正是"把外部依赖收进插件内部"的收益：内核不用再传 Judge 进来）
    static Judge judge;
    return std::make_shared<EngineHandle>("Minimax++",
                                          std::make_unique<MinimaxPP>(judge));
}

std::shared_ptr<IEngineHandle> makeTactical() {
    static Judge judge;
    return std::make_shared<EngineHandle>("TacticalMax",
                                          std::make_unique<TacticalMax>(judge));
}

// TacticalMax 支持时间预算；其它引擎忽略之（std::function 为空即不调用）
void setTacticalBudget(IEngineHandle* h, int ms) {
    auto* eh = dynamic_cast<EngineHandle*>(h);
    if (!eh) return;
    if (auto* tm = dynamic_cast<TacticalMax*>(eh->raw()))
        tm->setTimeBudgetMs(ms);
}

}  // namespace

// ============================================================
// EnginePlayer
// ============================================================
EnginePlayer::EnginePlayer(EngineBinding binding) : binding_(std::move(binding)) {
    if (binding_.create) engine_ = binding_.create();
}

EnginePlayer::~EnginePlayer() = default;

void EnginePlayer::onMatchStart(const ViewState&) {
    // 每局开新：重建引擎，清掉置换表与内部缓存，避免上一局的残留影响棋力。
    // （这是契约层 onMatchStart 存在的意义之一。）
    if (binding_.create) engine_ = binding_.create();
}

Decision EnginePlayer::tick(const ViewState& state, const ThinkBudget& budget) {
    if (!engine_) return Decision::pass("engine unavailable");

    if (binding_.setBudget && budget.timeBudgetMs > 0)
        binding_.setBudget(engine_.get(), budget.timeBudgetMs);

    const Coord c = engine_->decide(state.boardSize, state.winLength,
                                   state.history, state.turn);
    if (!c.valid()) return Decision::pass("engine found no move");

    // 防御：引擎不应该给出非法点，但契约层不信任插件 —— 这里先自查一次，
    // 真正权威的合法性判定仍在内核 GomokuRules::legal。
    if (c.r < 0 || c.c < 0 || c.r >= state.boardSize || c.c >= state.boardSize ||
        state.at(c.r, c.c) != Stone::Empty) {
        return Decision::pass("engine returned an illegal point");
    }
    return Decision::at(c.r, c.c);
}

// ============================================================
// 内置引擎绑定表
//
// 【新增引擎只有两步】
//   1) 在上面写一个 makeXxx() 返回 std::shared_ptr<IEngineHandle>
//   2) 在下面 kEngineBindings 里加一项
// 注册表与所有界面会自动同步，不需要在任何别的地方改代码。
// ============================================================
std::vector<EngineBinding> builtinEngineBindings() {
    std::vector<EngineBinding> v;

    v.push_back(EngineBinding{
        "easy-judge", "EasyJudge", "最弱：只做威胁判定 + 随机",
        [] { return makeEasy(); }, {} });

    v.push_back(EngineBinding{
        "puregreed-1.0", "PureGreed 1.0", "常规评分纯防守（只堵不建）",
        [] { return makePureGreed(0.0, "PureGreed 1.0"); }, {} });

    v.push_back(EngineBinding{
        "puregreed-1.1", "PureGreed 1.1", "常规评分攻防同权",
        [] { return makePureGreed(1.0, "PureGreed 1.1"); }, {} });

    v.push_back(EngineBinding{
        "minimax", "Minimax++", "α-β 搜索 + 置换表",
        [] { return makeMinimax(); }, {} });

    v.push_back(EngineBinding{
        "tactical-max", "TacticalMax", "最强：增量评估 + PVS + VCF/VCT",
        [] { return makeTactical(); }, setTacticalBudget });

    return v;
}

}  // namespace gomoku
