// A match between two UCI engines, refereed with StockCrab's own move generator (checked by perft).
//
//   match <engine A> <engine B> --games N --tc 10+0.1 --concurrency 6 --a "Opt=Val" --b "UCI_LimitStrength=true"
//
// Every opening is played twice, colours swapped. Prints A's wins, draws, losses and the Elo difference.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/board.h"

namespace {

// balanced positions a few moves in, so games differ
const char *OPENINGS[] = {
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkb1r/pppppppp/5n2/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 1 2",
    "rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1",
    "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
    "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
    "rnbqkb1r/pp2pppp/3p1n2/8/3NP3/8/PPP2PPP/RNBQKB1R w KQkq - 1 5",
    "rnbqkbnr/ppp2ppp/4p3/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/pp2pppp/2p5/3p4/3PP3/8/PPP2PPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkb1r/pppp1ppp/4pn2/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkb1r/pppppp1p/5np1/8/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/ppp1pppp/8/3p4/2PP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2",
};

struct Engine {
    pid_t pid = -1;
    int in = -1, out = -1;
    std::string buf;

    bool start(const std::string &path) {
        int to[2], from[2];
        if (pipe(to) || pipe(from)) return false;
        pid = fork();
        if (pid == 0) {
            dup2(to[0], 0);
            dup2(from[1], 1);
            int null = open("/dev/null", O_WRONLY);
            dup2(null, 2);
            close(to[1]);
            close(from[0]);
            execl("/bin/sh", "sh", "-c", ("exec " + path).c_str(), (char *)nullptr);
            _exit(127);
        }
        close(to[0]);
        close(from[1]);
        in = to[1];
        out = from[0];
        return pid > 0;
    }
    void send(const std::string &s) {
        std::string l = s + "\n";
        if (write(in, l.data(), l.size()) < 0) {}
    }
    // a line, or "" when timeout_ms passes
    std::string line(int timeout_ms) {
        while (true) {
            auto nl = buf.find('\n');
            if (nl != std::string::npos) {
                std::string l = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                if (l.empty()) continue; // a blank line from the engine is not a timeout
                return l;
            }
            pollfd p{out, POLLIN, 0};
            if (poll(&p, 1, timeout_ms) <= 0) return "";
            char tmp[4096];
            ssize_t n = read(out, tmp, sizeof tmp);
            if (n <= 0) return "";
            buf.append(tmp, n);
        }
    }
    bool wait_for(const std::string &word, int timeout_ms) {
        for (int i = 0; i < 100000; i++) {
            std::string l = line(timeout_ms);
            if (l.empty() && i > 0 && timeout_ms) return false;
            if (l.rfind(word, 0) == 0) return true;
        }
        return false;
    }
    void stop() {
        if (pid > 0) {
            send("quit");
            usleep(50000);
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        pid = -1;
    }
};

struct Config {
    std::string a, b;
    std::vector<std::string> a_opts, b_opts;
    int games = 40, concurrency = 6;
    int64_t base_ms = 10000, inc_ms = 100;
};

Config cfg;
std::mutex lock;
int done = 0, a_win = 0, draw = 0, a_loss = 0;
std::map<std::string, int> reasons;

bool setup(Engine &e, const std::string &path, const std::vector<std::string> &opts) {
    if (!e.start(path)) return false;
    e.send("uci");
    if (!e.wait_for("uciok", 10000)) return false;
    for (auto &o : opts) {
        auto eq = o.find('=');
        e.send("setoption name " + o.substr(0, eq) + " value " + o.substr(eq + 1));
    }
    e.send("isready");
    return e.wait_for("readyok", 10000);
}

int count_key(const Position &p) {
    int n = 1;
    for (auto &u : p.history)
        if (u.key == p.key) n++;
    return n;
}

// 1 = A wins, 0 = draw, -1 = A loses
int play(int index) {
    const char *fen = OPENINGS[(index / 2) % (sizeof OPENINGS / sizeof *OPENINGS)];
    bool a_white_first = index % 2 == 0;
    Engine e[2];
    if (!setup(e[0], cfg.a, cfg.a_opts) || !setup(e[1], cfg.b, cfg.b_opts)) {
        e[0].stop();
        e[1].stop();
        return 0;
    }
    Position pos;
    pos.set_fen(fen);
    // engine playing white: A if a_white_first
    int white_engine = a_white_first ? 0 : 1;
    int64_t clock[2] = {cfg.base_ms, cfg.base_ms};
    std::string moves;
    int result = 0; // from White: 1, 0, -1
    std::string why = "max length";
    for (auto &x : e) x.send("ucinewgame");
    for (int ply = 0; ply < 400; ply++) {
        MoveList l;
        pos.generate(l);
        int legal = 0;
        for (int i = 0; i < l.size; i++)
            if (pos.make(l.moves[i])) {
                pos.unmake();
                legal++;
            }
        if (!legal) {
            if (pos.in_check()) {
                result = pos.side == WHITE ? -1 : 1;
                why = "mate";
            } else why = "stalemate";
            break;
        }
        if (pos.halfmove >= 100) { why = "50 moves"; break; }
        if (count_key(pos) >= 3) { why = "repetition"; break; }
        if (pos.insufficient_material()) { why = "material"; break; }

        int side = pos.side;
        Engine &en = e[side == WHITE ? white_engine : 1 - white_engine];
        en.send("position fen " + std::string(fen) + (moves.empty() ? "" : " moves" + moves));
        int ci = side; // clock by colour
        en.send("go wtime " + std::to_string(clock[WHITE]) + " btime " + std::to_string(clock[BLACK]) + " winc " +
                std::to_string(cfg.inc_ms) + " binc " + std::to_string(cfg.inc_ms));
        auto t0 = std::chrono::steady_clock::now();
        std::string best;
        while (true) {
            std::string ln = en.line(int(clock[ci] + 2000));
            if (ln.empty()) break;
            if (ln.rfind("bestmove", 0) == 0) {
                best = ln.substr(9, ln.find(' ', 9) - 9);
                break;
            }
        }
        int64_t used = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        clock[ci] -= used;
        if (clock[ci] < 0 || best.empty()) {
            result = side == WHITE ? -1 : 1;
            why = best.empty() ? "no move" : "time";
            break;
        }
        clock[ci] += cfg.inc_ms;
        Move m = pos.parse_uci(best);
        if (m == NO_MOVE || !pos.make(m)) {
            result = side == WHITE ? -1 : 1;
            why = "illegal move " + best;
            break;
        }
        moves += " " + best;
    }
    for (auto &x : e) x.stop();
    int a_result = a_white_first ? result : -result;
    std::lock_guard<std::mutex> g(lock);
    reasons[why]++;
    return a_result;
}

} // namespace

int main(int argc, char **argv) {
    attacks::init();
    if (argc < 3) {
        std::fprintf(stderr, "usage: match <engine A> <engine B> [--games N] [--tc base+inc] [--concurrency N] [--a Opt=Val] [--b Opt=Val]\n");
        return 2;
    }
    cfg.a = argv[1];
    cfg.b = argv[2];
    for (int i = 3; i + 1 < argc; i += 2) {
        std::string k = argv[i], v = argv[i + 1];
        if (k == "--games") cfg.games = std::stoi(v);
        else if (k == "--concurrency") cfg.concurrency = std::stoi(v);
        else if (k == "--tc") {
            auto plus = v.find('+');
            cfg.base_ms = int64_t(std::stod(v.substr(0, plus)) * 1000);
            cfg.inc_ms = plus == std::string::npos ? 0 : int64_t(std::stod(v.substr(plus + 1)) * 1000);
        } else if (k == "--a") cfg.a_opts.push_back(v);
        else if (k == "--b") cfg.b_opts.push_back(v);
    }
    signal(SIGPIPE, SIG_IGN);
    std::vector<std::thread> pool;
    int next = 0;
    for (int t = 0; t < cfg.concurrency; t++)
        pool.emplace_back([&] {
            while (true) {
                int idx;
                {
                    std::lock_guard<std::mutex> g(lock);
                    if (next >= cfg.games) return;
                    idx = next++;
                }
                int r = play(idx);
                std::lock_guard<std::mutex> g(lock);
                (r > 0 ? a_win : r < 0 ? a_loss : draw)++;
                done++;
                std::fprintf(stderr, "\r%d/%d  +%d =%d -%d   ", done, cfg.games, a_win, draw, a_loss);
            }
        });
    for (auto &t : pool) t.join();
    double score = (a_win + 0.5 * draw) / std::max(1, done);
    double elo = score <= 0 ? -999 : score >= 1 ? 999 : -400 * std::log10(1 / score - 1);
    // 95% error from the per-game variance
    double var = (a_win * std::pow(1 - score, 2) + draw * std::pow(0.5 - score, 2) + a_loss * std::pow(score, 2)) / std::max(1, done);
    double se = std::sqrt(var / std::max(1, done));
    double lo = std::clamp(score - 1.96 * se, 0.001, 0.999), hi = std::clamp(score + 1.96 * se, 0.001, 0.999);
    double elo_lo = -400 * std::log10(1 / lo - 1), elo_hi = -400 * std::log10(1 / hi - 1);
    std::printf("\nA: %s\nB: %s\n%d games: +%d =%d -%d  score %.1f%%  Elo %+.0f  (95%%: %+.0f .. %+.0f)\n", cfg.a.c_str(), cfg.b.c_str(), done,
                a_win, draw, a_loss, score * 100, elo, elo_lo, elo_hi);
    std::printf("endings:");
    for (auto &[k, v] : reasons) std::printf(" %s %d,", k.c_str(), v);
    std::printf("\n");
    return 0;
}
