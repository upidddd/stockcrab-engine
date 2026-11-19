// Move generation against the standard perft positions (chessprogramming.org/Perft_Results).
#include <chrono>
#include <cstdio>

#include "../src/board.h"

struct Case {
    const char *name, *fen;
    int depth;
    uint64_t nodes;
};

int main() {
    attacks::init();
    const Case cases[] = {
        {"start", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
        {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
        {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083},
        {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
        {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
        {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
    };
    int bad = 0;
    uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (auto &c : cases) {
        Position pos;
        pos.set_fen(c.fen);
        uint64_t n = perft_ns::perft(pos, c.depth);
        total += n;
        bool ok = n == c.nodes && pos.fen() == std::string(c.fen) && pos.key == pos.compute_key();
        std::printf("%-11s depth %d  %10llu  %s\n", c.name, c.depth, (unsigned long long)n, ok ? "ok" : "WRONG");
        if (!ok) {
            std::printf("   expected %llu, fen after: %s\n", (unsigned long long)c.nodes, pos.fen().c_str());
            bad++;
        }
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%s, %.1f M nodes/s\n", bad ? "FAILED" : "all ok", total / s / 1e6);
    return bad ? 1 : 0;
}
