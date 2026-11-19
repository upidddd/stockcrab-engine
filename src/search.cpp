// StockCrab: search.
//
// Iterative deepening, aspiration windows, principal variation search with a transposition table, null move
// pruning, reverse futility pruning, late move reductions and pruning, check extension, and a quiescence search
// over captures. Move ordering: hash move, captures by MVV-LVA, killers, history. The character's `risk` changes
// how hard quiet moves are reduced, and its `contempt` how a draw is scored.

#include "search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <sstream>

#include "eval.h"

namespace search {

std::atomic<bool> stop{false};

namespace {

using Clock = std::chrono::steady_clock;

/* ---------- transposition table ---------- */

enum : uint8_t { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

struct Entry {
    U64 key;
    Move move;
    int16_t score;
    int8_t depth;
    uint8_t bound;
    uint8_t age;
};

std::vector<Entry> tt;
U64 tt_mask = 0;
uint8_t tt_age = 0;

int to_tt(int s, int ply) { return s > MATE - MAX_PLY ? s + ply : s < -MATE + MAX_PLY ? s - ply : s; }
int from_tt(int s, int ply) { return s > MATE - MAX_PLY ? s - ply : s < -MATE + MAX_PLY ? s + ply : s; }

Entry *probe(U64 key) {
    Entry *e = &tt[key & tt_mask];
    return e->key == key ? e : nullptr;
}

void store(U64 key, Move m, int score, int depth, int bound, int ply) {
    Entry &e = tt[key & tt_mask];
    // keep a deeper entry of the same search unless this one is exact
    if (e.key == key && e.age == tt_age && depth < e.depth - 2 && bound != BOUND_EXACT) return;
    if (m == NO_MOVE && e.key == key) m = e.move;
    e = Entry{key, m, int16_t(to_tt(score, ply)), int8_t(depth), uint8_t(bound), tt_age};
}

int hashfull() {
    int used = 0;
    for (size_t i = 0; i < 1000 && i < tt.size(); i++) used += tt[i].key != 0 && tt[i].age == tt_age;
    return used;
}

/* ---------- search state ---------- */

Position *P;
uint64_t nodes;
int seldepth;
Clock::time_point started;
int64_t soft_ms, hard_ms;
uint64_t node_limit;
bool timed;
int root_side;
int contempt;

Move killers[MAX_PLY][2];
int history[2][64][64];
Move pv[MAX_PLY][MAX_PLY];
int pv_len[MAX_PLY];
int lmr[64][64];

int64_t elapsed() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count(); }

void check_limits() {
    if ((timed && elapsed() >= hard_ms) || (node_limit && nodes >= node_limit)) stop = true;
}

int draw_score() { return P->side == root_side ? -contempt : contempt; }

/* ---------- move ordering ---------- */

int victim_of(Move m) {
    if (move_flag(m) == EP_CAPTURE) return PAWN;
    int p = P->board[move_to(m)];
    return p == NO_PIECE ? PAWN : type_of(p);
}

void score_moves(const MoveList &l, int *scores, Move tt_move, int ply) {
    for (int i = 0; i < l.size; i++) {
        Move m = l.moves[i];
        int s;
        if (m == tt_move) s = 2000000;
        else if (is_capture(m) || is_promo(m)) {
            s = 1000000 + (is_capture(m) ? eval::piece_value[victim_of(m)] * 10 - eval::piece_value[type_of(P->board[move_from(m)])] / 10 : 0);
            if (is_promo(m)) s += promo_type(m) == QUEEN ? 9000 : -900000;
        } else if (m == killers[ply][0]) s = 900000;
        else if (m == killers[ply][1]) s = 800000;
        else s = history[P->side][move_from(m)][move_to(m)];
        scores[i] = s;
    }
}

Move pick(MoveList &l, int *scores, int i) {
    int best = i;
    for (int j = i + 1; j < l.size; j++)
        if (scores[j] > scores[best]) best = j;
    std::swap(l.moves[i], l.moves[best]);
    std::swap(scores[i], scores[best]);
    return l.moves[i];
}

/* ---------- quiescence ---------- */

int qsearch(int alpha, int beta, int ply) {
    if ((++nodes & 2047) == 0) check_limits();
    if (stop) return 0;
    if (ply > seldepth) seldepth = ply;
    if (ply >= MAX_PLY - 1) return eval::evaluate(*P);

    const bool in_check = P->in_check();
    int best = -INF;
    if (!in_check) {
        int stand = eval::evaluate(*P);
        if (stand >= beta) return stand;
        if (stand > alpha) alpha = stand;
        best = stand;
    }

    MoveList l;
    P->generate(l, !in_check); // in check: every evasion, so mates are seen
    int scores[256];
    score_moves(l, scores, NO_MOVE, ply);
    int legal = 0;
    for (int i = 0; i < l.size; i++) {
        Move m = pick(l, scores, i);
        // delta pruning: even winning this piece would not bring the score up to alpha
        if (!in_check && !is_promo(m) && best + eval::piece_value[victim_of(m)] + 200 <= alpha) continue;
        if (!P->make(m)) continue;
        legal++;
        int s = -qsearch(-beta, -alpha, ply + 1);
        P->unmake();
        if (stop) return 0;
        if (s > best) {
            best = s;
            if (s > alpha) {
                alpha = s;
                if (s >= beta) break;
            }
        }
    }
    if (in_check && legal == 0) return -MATE + ply;
    return best;
}

/* ---------- main search ---------- */

bool has_pieces(int c) {
    return P->pieces(c, KNIGHT) | P->pieces(c, BISHOP) | P->pieces(c, ROOK) | P->pieces(c, QUEEN);
}

int negamax(int depth, int alpha, int beta, int ply, bool can_null, const std::vector<Move> *excluded = nullptr) {
    const bool root = ply == 0;
    const bool pv_node = beta - alpha > 1;
    pv_len[ply] = ply;

    if (!root) {
        if (P->halfmove >= 100 || P->is_repetition() || P->insufficient_material()) return draw_score();
        // mate distance pruning
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    const bool in_check = P->in_check();
    if (in_check) depth++;
    if (depth <= 0) return qsearch(alpha, beta, ply);
    if ((++nodes & 2047) == 0) check_limits();
    if (stop) return 0;
    if (ply >= MAX_PLY - 1) return eval::evaluate(*P);

    Move tt_move = NO_MOVE;
    if (Entry *e = probe(P->key)) {
        tt_move = e->move;
        if (!pv_node && e->depth >= depth) {
            int s = from_tt(e->score, ply);
            if (e->bound == BOUND_EXACT || (e->bound == BOUND_LOWER && s >= beta) || (e->bound == BOUND_UPPER && s <= alpha))
                return s;
        }
    }

    const int static_eval = in_check ? -INF : eval::evaluate(*P);

    if (!pv_node && !in_check) {
        // reverse futility: far above beta with little depth left
        if (depth <= 7 && static_eval - 75 * depth >= beta && std::abs(beta) < MATE - MAX_PLY) return static_eval;
        // null move: if passing still holds beta, a real move will too
        if (can_null && depth >= 3 && static_eval >= beta && has_pieces(P->side)) {
            int r = 3 + depth / 4 + std::min(3, (static_eval - beta) / 200);
            P->make_null();
            int s = -negamax(depth - 1 - r, -beta, -beta + 1, ply + 1, false);
            P->unmake_null();
            if (stop) return 0;
            if (s >= beta) return std::abs(s) > MATE - MAX_PLY ? beta : s;
        }
    }

    MoveList l;
    P->generate(l);
    int scores[256];
    score_moves(l, scores, tt_move, ply);

    const int risk = eval::character().risk;
    int best = -INF, legal = 0, searched_quiets = 0;
    Move best_move = NO_MOVE;
    const int old_alpha = alpha;

    for (int i = 0; i < l.size; i++) {
        Move m = pick(l, scores, i);
        if (excluded && std::find(excluded->begin(), excluded->end(), m) != excluded->end()) continue;
        const bool quiet = !is_capture(m) && !is_promo(m);

        // late move pruning: at low depth, quiet moves far down the list rarely matter
        if (!root && !pv_node && !in_check && quiet && depth <= 3 && best > -MATE + MAX_PLY &&
            searched_quiets > 3 + depth * depth * 2)
            continue;

        if (!P->make(m)) continue;
        legal++;
        if (quiet) searched_quiets++;

        int s;
        if (legal == 1) s = -negamax(depth - 1, -beta, -alpha, ply + 1, true);
        else {
            int r = 0;
            if (depth >= 3 && quiet && !in_check && legal > (pv_node ? 4 : 2)) {
                r = lmr[std::min(depth, 63)][std::min(legal, 63)] * risk / 100;
                if (pv_node) r--;
                if (m == killers[ply][0] || m == killers[ply][1]) r--;
                r = std::clamp(r, 0, depth - 2);
            }
            s = -negamax(depth - 1 - r, -alpha - 1, -alpha, ply + 1, true);
            if (s > alpha && r > 0) s = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1, true);
            if (s > alpha && s < beta) s = -negamax(depth - 1, -beta, -alpha, ply + 1, true);
        }
        P->unmake();
        if (stop) return 0;

        if (s > best) {
            best = s;
            best_move = m;
            if (s > alpha) {
                alpha = s;
                pv[ply][ply] = m;
                for (int k = ply + 1; k < pv_len[ply + 1]; k++) pv[ply][k] = pv[ply + 1][k];
                pv_len[ply] = pv_len[ply + 1];
                if (s >= beta) {
                    if (quiet) {
                        if (killers[ply][0] != m) {
                            killers[ply][1] = killers[ply][0];
                            killers[ply][0] = m;
                        }
                        int &h = history[P->side][move_from(m)][move_to(m)];
                        h += depth * depth;
                        if (h > 400000) // keep history below the killer scores
                            for (auto &a : history)
                                for (auto &b : a)
                                    for (int &c : b) c /= 2;
                    }
                    break;
                }
            }
        }
    }

    if (legal == 0) {
        if (excluded) return -INF; // every move is excluded (MultiPV ran out of moves)
        return in_check ? -MATE + ply : draw_score();
    }
    if (!excluded)
        store(P->key, best_move, best, depth, best >= beta ? BOUND_LOWER : best > old_alpha ? BOUND_EXACT : BOUND_UPPER, ply);
    return best;
}

std::string score_str(int s) {
    if (s > MATE - MAX_PLY) return "mate " + std::to_string((MATE - s + 1) / 2);
    if (s < -MATE + MAX_PLY) return "mate -" + std::to_string((MATE + s) / 2);
    return "cp " + std::to_string(s);
}

/* ---------- the crab talks ---------- */

std::mt19937 rng{std::random_device{}()};
int last_score = 0;
bool have_last = false;

std::string comment(int score, int prev) {
    auto pick_one = [](std::initializer_list<const char *> l) { return std::string(*(l.begin() + rng() % l.size())); };
    if (score > MATE - MAX_PLY) return pick_one({"Mate is coming. Snip snip.", "Count your moves. Not many left.", "Claws out. It is over."});
    if (score < -MATE + MAX_PLY) return pick_one({"...fine. Well played. I am still angry.", "I see it. I do not like it."});
    int d = score - prev;
    if (d >= 250) return pick_one({"You blundered. I noticed.", "Thank you for the free material.", "That was a mistake. A big one."});
    if (d >= 100) return pick_one({"Getting better for me.", "Hm. That helped me.", "Careful now."});
    if (d <= -250) return pick_one({"WHAT. Where did that come from.", "I hate this move.", "Okay. Now I am really angry."});
    if (d <= -100) return pick_one({"Not bad. Annoying, but not bad.", "You found something. Grr."});
    if (score > 300) return pick_one({"I am winning and you know it.", "Resign? No? Fine."});
    if (score < -300) return pick_one({"Do not get excited. Crabs bite back.", "Losing. For now."});
    return "";
}

} // namespace

void init() {
    for (int d = 0; d < 64; d++)
        for (int m = 0; m < 64; m++) lmr[d][m] = (d && m) ? int(0.75 + std::log(d) * std::log(m) / 2.25) : 0;
    set_hash(64);
}

void set_hash(int mb) {
    size_t n = 1;
    while (n * 2 * sizeof(Entry) <= size_t(std::max(1, mb)) * 1024 * 1024) n *= 2;
    std::vector<Entry>().swap(tt); // give the old table's memory back before taking the new one
    tt.assign(n, Entry{});
    tt_mask = n - 1;
}

void clear() {
    std::fill(tt.begin(), tt.end(), Entry{});
    std::memset(history, 0, sizeof history);
    std::memset(killers, 0, sizeof killers);
    have_last = false;
}

Move go(Position &pos, const Limits &lim, const Options &opt, const std::function<void(const std::string &)> &report) {
    P = &pos;
    stop = false;
    nodes = 0;
    started = Clock::now();
    tt_age++;
    root_side = pos.side;
    contempt = eval::character().contempt;
    std::memset(killers, 0, sizeof killers);
    for (auto &a : history)
        for (auto &b : a)
            for (int &c : b) c /= 8;

    // legal root moves
    std::vector<Move> root_moves;
    {
        MoveList l;
        pos.generate(l);
        for (int i = 0; i < l.size; i++)
            if (pos.make(l.moves[i])) {
                pos.unmake();
                root_moves.push_back(l.moves[i]);
            }
    }
    if (root_moves.empty()) {
        report("info depth 0 score " + std::string(pos.in_check() ? "mate 0" : "cp 0"));
        return NO_MOVE;
    }

    // limits
    int max_depth = lim.depth > 0 ? std::min(lim.depth, MAX_PLY - 1) : MAX_PLY - 1;
    node_limit = lim.nodes;
    timed = false;
    if (lim.movetime > 0) {
        timed = true;
        soft_ms = hard_ms = std::max<int64_t>(1, lim.movetime - opt.move_overhead);
    } else if (!lim.infinite && lim.time[pos.side] > 0) {
        timed = true;
        int64_t left = std::max<int64_t>(1, lim.time[pos.side] - opt.move_overhead);
        int mtg = lim.movestogo > 0 ? std::min(lim.movestogo, 40) : 30;
        soft_ms = left / mtg + lim.inc[pos.side] * 3 / 4;
        hard_ms = std::min<int64_t>(left * 6 / 10, soft_ms * 4);
        soft_ms = std::min(soft_ms, hard_ms);
    }

    // strength limit: shallower search and a pick among the best moves that is less exact the lower the Elo
    int multipv = std::clamp(opt.multipv, 1, int(root_moves.size()));
    int internal_pv = multipv;
    double temperature = 0;
    if (opt.limit_strength) {
        int elo = std::clamp(opt.elo, 200, 3000);
        max_depth = std::min(max_depth, std::clamp(1 + (elo - 200) / 160, 1, 20));
        temperature = std::max(0, 2800 - elo) * 0.11; // centipawns
        internal_pv = std::max(internal_pv, std::min(6, int(root_moves.size())));
    }
    bool vary = opt.variety > 0 && pos.fullmove <= 12 && !opt.limit_strength;
    if (vary) internal_pv = std::max(internal_pv, std::min(4, int(root_moves.size())));

    std::vector<std::pair<int, Move>> results; // score, move for each PV line of the last complete depth
    std::vector<std::vector<Move>> lines;
    Move best = root_moves[0];
    int best_score = 0;
    int last_depth = 0;

    if (root_moves.size() == 1 && !lim.infinite && timed) max_depth = std::min(max_depth, 4);

    for (int depth = 1; depth <= max_depth; depth++) {
        seldepth = 0;
        std::vector<Move> excluded;
        std::vector<std::pair<int, Move>> this_depth;
        std::vector<std::vector<Move>> this_lines;
        for (int k = 0; k < internal_pv; k++) {
            int prev = k < int(results.size()) ? results[k].first : 0;
            int s;
            int window = 25;
            int alpha = -INF, beta = INF;
            if (depth >= 5 && k < int(results.size())) {
                alpha = prev - window;
                beta = prev + window;
            }
            while (true) {
                s = negamax(depth, alpha, beta, 0, true, excluded.empty() ? nullptr : &excluded);
                if (stop) break;
                if (s <= alpha) {
                    alpha = std::max(-INF, alpha - window);
                    window *= 2;
                } else if (s >= beta) {
                    beta = std::min(INF, beta + window);
                    window *= 2;
                } else break;
                if (window > 1000) {
                    alpha = -INF;
                    beta = INF;
                }
            }
            if (stop) break;
            if (s == -INF || pv_len[0] == 0) break;
            Move m = pv[0][0];
            this_depth.push_back({s, m});
            this_lines.push_back(std::vector<Move>(pv[0], pv[0] + pv_len[0]));
            excluded.push_back(m);
        }
        if (stop && this_depth.empty()) break;
        // a partly searched depth still improves the lines it finished
        for (size_t k = 0; k < this_depth.size(); k++) {
            if (k < results.size()) {
                results[k] = this_depth[k];
                lines[k] = this_lines[k];
            } else {
                results.push_back(this_depth[k]);
                lines.push_back(this_lines[k]);
            }
        }
        std::stable_sort(results.begin(), results.end(), [](auto &a, auto &b) { return a.first > b.first; });
        // keep lines matching the sorted results
        {
            std::vector<std::vector<Move>> sorted;
            for (auto &r : results)
                for (auto &ln : lines)
                    if (!ln.empty() && ln[0] == r.second) {
                        sorted.push_back(ln);
                        break;
                    }
            lines = sorted;
        }
        best = results[0].second;
        best_score = results[0].first;
        last_depth = depth;

        int64_t ms = elapsed();
        for (int k = 0; k < std::min<int>(multipv, results.size()); k++) {
            std::ostringstream o;
            o << "info depth " << depth << " seldepth " << seldepth;
            if (multipv > 1) o << " multipv " << k + 1;
            o << " score " << score_str(results[k].first) << " nodes " << nodes << " nps "
              << (ms ? nodes * 1000 / ms : nodes) << " hashfull " << hashfull() << " time " << ms << " pv";
            for (Move m : lines[k]) o << ' ' << pos.move_to_uci(m);
            report(o.str());
        }
        if (stop) break;
        if (timed && elapsed() >= soft_ms * 6 / 10) break;
        if (std::abs(best_score) > MATE - MAX_PLY && depth > 8) break;
    }

    // strength limit or variety: choose among the lines found, by how much each gives away
    if ((temperature > 0 || vary) && results.size() > 1) {
        double t = temperature > 0 ? temperature : opt.variety * 0.3;
        double top = results[0].first;
        std::vector<double> w;
        for (auto &r : results) {
            double loss = top - r.first;
            if (vary && temperature == 0 && loss > opt.variety * 0.6) {
                w.push_back(0);
                continue;
            }
            w.push_back(std::exp(-loss / std::max(1.0, t)));
        }
        std::discrete_distribution<int> d(w.begin(), w.end());
        int k = d(rng);
        best = results[k].second;
        best_score = results[k].first;
        // the move it picked may be one of the lines it searched but did not print: print it, so a GUI has its score
        if (k >= std::min<int>(multipv, results.size()) && k < int(lines.size())) {
            int64_t ms = elapsed();
            std::ostringstream o;
            o << "info depth " << last_depth << " seldepth " << seldepth << " multipv " << k + 1 << " score " << score_str(best_score)
              << " nodes " << nodes << " nps " << (ms ? nodes * 1000 / ms : nodes) << " time " << ms << " pv";
            for (Move m : lines[k]) o << ' ' << pos.move_to_uci(m);
            report(o.str());
        }
    }

    if (opt.talk) {
        std::string c = have_last ? comment(best_score, last_score) : "";
        if (!c.empty()) report("info string crab: " + c);
    }
    last_score = best_score;
    have_last = true;
    return best;
}

} // namespace search
