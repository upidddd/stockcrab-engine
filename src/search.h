// StockCrab: search.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "board.h"

constexpr int INF = 32000;
constexpr int MATE = 31000;
constexpr int MAX_PLY = 128;

struct Limits {
    int64_t time[2] = {0, 0}, inc[2] = {0, 0};
    int movestogo = 0;
    int depth = 0;
    int64_t movetime = 0;
    uint64_t nodes = 0;
    bool infinite = false;
};

struct Options {
    int hash_mb = 64;
    int multipv = 1;
    int move_overhead = 30;
    bool limit_strength = false;
    int elo = 3000;      // 200 .. 3000 when limit_strength is on
    bool talk = false;   // the crab comments on the game in "info string"
    int variety = 0;     // 0..100: how often it picks a slightly worse but different move in the opening
};

namespace search {
void init();
void set_hash(int mb);
void clear();
// runs in the calling thread; `report` gets every UCI line; returns the best move
Move go(Position &pos, const Limits &limits, const Options &opt, const std::function<void(const std::string &)> &report);
extern std::atomic<bool> stop;
} // namespace search
