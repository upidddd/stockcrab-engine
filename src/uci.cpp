// StockCrab: UCI commands, shared by the native program and the browser build.

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include "board.h"
#include "eval.h"
#include "search.h"
#include "uci.h"

namespace {

const char *START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char *NAME = "StockCrab 1.0";

std::mutex out_lock;
uci::Writer writer = [](const std::string &s) { std::cout << s << std::endl; };
void say(const std::string &s) {
    std::lock_guard<std::mutex> g(out_lock);
    writer(s);
}
bool threaded = true;

Position pos;
Options opt;
Character ch;
std::thread worker;

void wait_search() {
    if (worker.joinable()) worker.join();
}

void print_options() {
    say("id name " + std::string(NAME));
    say("id author upidddd");
    say("option name Hash type spin default 64 min 1 max 4096");
    say("option name Clear Hash type button");
    say("option name MultiPV type spin default 1 min 1 max 64");
    say("option name Move Overhead type spin default 30 min 0 max 5000");
    say("option name UCI_LimitStrength type check default false");
    say("option name UCI_Elo type spin default 3000 min 200 max 3000");
    // the character
    say(std::string("option name Personality type combo default Balanced var ") + eval::preset_names());
    say("option name Aggression type spin default 100 min 0 max 300");
    say("option name Caution type spin default 100 min 0 max 300");
    say("option name Greed type spin default 100 min 50 max 200");
    say("option name Activity type spin default 100 min 0 max 300");
    say("option name Pawn Structure type spin default 100 min 0 max 300");
    say("option name Contempt type spin default 0 min -200 max 200");
    say("option name Risk type spin default 100 min 50 max 200");
    say("option name Favourite Piece type combo default None var None var Knight var Bishop var Rook var Queen");
    say("option name Variety type spin default 0 min 0 max 100");
    say("option name Talk type check default false");
    say("uciok");
}

int to_int(const std::string &v, int def) {
    char *end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    return end == v.c_str() ? def : int(n);
}

void set_option(std::string name, const std::string &value) {
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    if (name == "hash") search::set_hash(std::clamp(to_int(value, 64), 1, 4096));
    else if (name == "clear hash") search::clear();
    else if (name == "multipv") opt.multipv = std::clamp(to_int(value, 1), 1, 64);
    else if (name == "move overhead") opt.move_overhead = std::clamp(to_int(value, 30), 0, 5000);
    else if (name == "uci_limitstrength") opt.limit_strength = value == "true";
    else if (name == "uci_elo") opt.elo = std::clamp(to_int(value, 3000), 200, 3000);
    else if (name == "talk") opt.talk = value == "true";
    else if (name == "variety") opt.variety = std::clamp(to_int(value, 0), 0, 100);
    else if (name == "personality") {
        Character c;
        if (eval::load_preset(value, c)) ch = c;
    } else if (name == "aggression") ch.aggression = std::clamp(to_int(value, 100), 0, 300);
    else if (name == "caution") ch.caution = std::clamp(to_int(value, 100), 0, 300);
    else if (name == "greed") ch.greed = std::clamp(to_int(value, 100), 50, 200);
    else if (name == "activity") ch.activity = std::clamp(to_int(value, 100), 0, 300);
    else if (name == "pawn structure") ch.pawns = std::clamp(to_int(value, 100), 0, 300);
    else if (name == "contempt") ch.contempt = std::clamp(to_int(value, 0), -200, 200);
    else if (name == "risk") ch.risk = std::clamp(to_int(value, 100), 50, 200);
    else if (name == "favourite piece")
        ch.favourite = value == "Knight" ? KNIGHT : value == "Bishop" ? BISHOP : value == "Rook" ? ROOK : value == "Queen" ? QUEEN : -1;
    else {
        say("info string unknown option " + name);
        return;
    }
    eval::set_character(ch);
}

void position(std::istringstream &in) {
    std::string tok, fen;
    in >> tok;
    if (tok == "startpos") {
        fen = START;
        in >> tok; // "moves" or nothing
    } else if (tok == "fen") {
        while (in >> tok && tok != "moves") fen += tok + " ";
    }
    if (!pos.set_fen(fen)) {
        say("info string bad position");
        pos.set_fen(START);
        return;
    }
    while (in >> tok) {
        Move m = pos.parse_uci(tok);
        if (m == NO_MOVE || !pos.make(m)) {
            say("info string illegal move " + tok);
            break;
        }
    }
}

void go(std::istringstream &in) {
    Limits lim;
    std::string tok;
    while (in >> tok) {
        int64_t v = 0;
        if (tok == "infinite") {
            lim.infinite = true;
            continue;
        }
        if (!(in >> v)) break;
        if (tok == "wtime") lim.time[WHITE] = v;
        else if (tok == "btime") lim.time[BLACK] = v;
        else if (tok == "winc") lim.inc[WHITE] = v;
        else if (tok == "binc") lim.inc[BLACK] = v;
        else if (tok == "movestogo") lim.movestogo = int(v);
        else if (tok == "depth") lim.depth = int(v);
        else if (tok == "movetime") lim.movetime = v;
        else if (tok == "nodes") lim.nodes = uint64_t(v);
    }
    wait_search();
    Position copy = pos;
    auto run = [copy, lim]() mutable {
        Move m = search::go(copy, lim, opt, say);
        say("bestmove " + copy.move_to_uci(m));
    };
    if (threaded) worker = std::thread(run);
    else run(); // the browser build: the worker itself is the search thread
}

void bench() {
    // a fixed search over a few positions: the node count checks that a build behaves the same, the speed compares builds
    const char *fens[] = {
        START,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R2QK2R w KQ - 0 8",
    };
    uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const char *f : fens) {
        Position p;
        p.set_fen(f);
        search::clear();
        Limits lim;
        lim.depth = 12;
        uint64_t before = 0;
        search::go(p, lim, opt, [&](const std::string &s) {
            auto at = s.find(" nodes ");
            if (at != std::string::npos) before = std::stoull(s.substr(at + 7));
        });
        total += before;
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%llu nodes %.0f nps\n", (unsigned long long)total, total / sec);
}

} // namespace

namespace uci {

void init(Writer w, bool use_threads) {
    if (w) writer = w;
    threaded = use_threads;
    attacks::init();
    search::init();
    eval::set_character(ch);
    pos.set_fen(START);
}

const char *name() { return NAME; }

void run_bench() { bench(); }

// returns false on "quit"
bool command(const std::string &line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    if (cmd == "uci") print_options();
    else if (cmd == "isready") say("readyok");
    else if (cmd == "ucinewgame") {
        wait_search();
        search::clear();
    } else if (cmd == "position") {
        wait_search();
        position(in);
    } else if (cmd == "go") go(in);
    else if (cmd == "stop") {
        search::stop = true;
        wait_search();
    } else if (cmd == "quit") {
        search::stop = true;
        wait_search();
        return false;
    } else if (cmd == "setoption") {
        std::string tok, name, value;
        in >> tok; // "name"
        while (in >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
        while (in >> tok) value += (value.empty() ? "" : " ") + tok;
        wait_search();
        set_option(name, value);
    } else if (cmd == "d") say(pos.fen());
    else if (cmd == "perft") {
        int d = 1;
        in >> d;
        say(std::to_string(perft_ns::perft(pos, d)));
    } else if (cmd == "eval") say("eval " + std::to_string(eval::evaluate(pos)) + " cp (side to move)");
    return true;
}

void shutdown() {
    search::stop = true;
    wait_search();
}

} // namespace uci
