// StockCrab: attack tables, Zobrist keys, FEN, move generation, make/unmake.

#include "board.h"

#include <cstring>
#include <sstream>

/* ---------- attack tables ---------- */

namespace attacks {

U64 pawn[2][64], knight[64], king[64];

namespace {

struct Magic {
    U64 mask;
    U64 magic;
    int shift;
    U64 *table;
};

Magic bmagic[64], rmagic[64];
U64 btable[0x1480], rtable[0x19000];

// the slow way, used to build the tables
U64 slide(int s, U64 occ, const int dirs[4][2]) {
    U64 a = 0;
    for (int d = 0; d < 4; d++) {
        int f = sq_file(s), r = sq_rank(s);
        while (true) {
            f += dirs[d][0];
            r += dirs[d][1];
            if (f < 0 || f > 7 || r < 0 || r > 7) break;
            a |= bit(r * 8 + f);
            if (occ & bit(r * 8 + f)) break;
        }
    }
    return a;
}

const int BDIRS[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
const int RDIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// squares whose occupancy matters: the rays without their last square
U64 relevant(int s, const int dirs[4][2]) {
    U64 m = 0;
    for (int d = 0; d < 4; d++) {
        int f = sq_file(s) + dirs[d][0], r = sq_rank(s) + dirs[d][1];
        while (f + dirs[d][0] >= 0 && f + dirs[d][0] <= 7 && r + dirs[d][1] >= 0 && r + dirs[d][1] <= 7 && f >= 0 && f <= 7 &&
               r >= 0 && r <= 7) {
            m |= bit(r * 8 + f);
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return m;
}

U64 rng_state = 0x9E3779B97F4A7C15ULL;
U64 rng() {
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ULL;
}

void find_magics(Magic *m, U64 *table, const int dirs[4][2]) {
    U64 occ[4096], ref[4096];
    U64 *next = table;
    for (int s = 0; s < 64; s++) {
        Magic &mg = m[s];
        mg.mask = relevant(s, dirs);
        int bits = popcount(mg.mask);
        mg.shift = 64 - bits;
        mg.table = next;
        int n = 0;
        U64 b = 0;
        do { // every subset of the mask
            occ[n] = b;
            ref[n] = slide(s, b, dirs);
            n++;
            b = (b - mg.mask) & mg.mask;
        } while (b);
        std::vector<int> epoch(n, 0);
        std::vector<U64> used(n);
        for (int attempt = 1;; attempt++) {
            mg.magic = rng() & rng() & rng();
            if (popcount((mg.mask * mg.magic) >> 56) < 6) continue;
            bool ok = true;
            for (int i = 0; i < n && ok; i++) {
                unsigned idx = unsigned((occ[i] * mg.magic) >> mg.shift);
                if (epoch[idx] < attempt) {
                    epoch[idx] = attempt;
                    used[idx] = ref[i];
                } else if (used[idx] != ref[i]) ok = false;
            }
            if (ok) {
                for (int i = 0; i < n; i++) mg.table[(occ[i] * mg.magic) >> mg.shift] = ref[i];
                break;
            }
        }
        next += n;
    }
}

} // namespace

U64 bishop(int s, U64 occ) {
    const Magic &m = bmagic[s];
    return m.table[((occ & m.mask) * m.magic) >> m.shift];
}
U64 rook(int s, U64 occ) {
    const Magic &m = rmagic[s];
    return m.table[((occ & m.mask) * m.magic) >> m.shift];
}

void init() {
    for (int s = 0; s < 64; s++) {
        int f = sq_file(s), r = sq_rank(s);
        auto at = [](int ff, int rr) { return (ff >= 0 && ff < 8 && rr >= 0 && rr < 8) ? bit(rr * 8 + ff) : 0ULL; };
        pawn[WHITE][s] = at(f - 1, r + 1) | at(f + 1, r + 1);
        pawn[BLACK][s] = at(f - 1, r - 1) | at(f + 1, r - 1);
        knight[s] = at(f + 1, r + 2) | at(f + 2, r + 1) | at(f + 2, r - 1) | at(f + 1, r - 2) | at(f - 1, r - 2) |
                    at(f - 2, r - 1) | at(f - 2, r + 1) | at(f - 1, r + 2);
        king[s] = at(f + 1, r) | at(f - 1, r) | at(f, r + 1) | at(f, r - 1) | at(f + 1, r + 1) | at(f + 1, r - 1) |
                  at(f - 1, r + 1) | at(f - 1, r - 1);
    }
    find_magics(bmagic, btable, BDIRS);
    find_magics(rmagic, rtable, RDIRS);
}

} // namespace attacks

/* ---------- Zobrist keys ---------- */

namespace zobrist {
U64 piece[12][64], castling[16], ep[8], side;
struct Init {
    Init() {
        U64 s = 0x2545F4914F6CDD1DULL;
        auto next = [&] {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return s;
        };
        for (auto &p : piece)
            for (auto &k : p) k = next();
        for (auto &k : castling) k = next();
        for (auto &k : ep) k = next();
        side = next();
    }
} init_keys;
} // namespace zobrist

/* ---------- position ---------- */

// rights lost when a piece moves from or to a square
static const int castle_mask[64] = {
    13, 15, 15, 15, 12, 15, 15, 14, //
    15, 15, 15, 15, 15, 15, 15, 15, //
    15, 15, 15, 15, 15, 15, 15, 15, //
    15, 15, 15, 15, 15, 15, 15, 15, //
    15, 15, 15, 15, 15, 15, 15, 15, //
    15, 15, 15, 15, 15, 15, 15, 15, //
    15, 15, 15, 15, 15, 15, 15, 15, //
    7,  15, 15, 15, 3,  15, 15, 11,
};

void Position::put(int p, int s) {
    bb[p] |= bit(s);
    occ[color_of(p)] |= bit(s);
    board[s] = p;
    key ^= zobrist::piece[p][s];
}

void Position::remove(int s) {
    int p = board[s];
    bb[p] &= ~bit(s);
    occ[color_of(p)] &= ~bit(s);
    board[s] = NO_PIECE;
    key ^= zobrist::piece[p][s];
}

U64 Position::compute_key() const {
    U64 k = 0;
    for (int s = 0; s < 64; s++)
        if (board[s] != NO_PIECE) k ^= zobrist::piece[board[s]][s];
    k ^= zobrist::castling[castling];
    if (ep >= 0) k ^= zobrist::ep[sq_file(ep)];
    if (side == BLACK) k ^= zobrist::side;
    return k;
}

bool Position::set_fen(const std::string &fen) {
    std::memset(bb, 0, sizeof bb);
    occ[0] = occ[1] = 0;
    for (int &p : board) p = NO_PIECE;
    history.clear();
    key = 0;
    std::istringstream in(fen);
    std::string placement, stm, cast, eps;
    in >> placement >> stm >> cast >> eps >> halfmove >> fullmove;
    if (in.fail()) {
        // halfmove and fullmove are optional
        halfmove = 0;
        fullmove = 1;
    }
    int r = 7, f = 0;
    const std::string pcs = "PNBRQKpnbrqk";
    for (char c : placement) {
        if (c == '/') {
            r--;
            f = 0;
        } else if (c >= '1' && c <= '8') f += c - '0';
        else {
            auto i = pcs.find(c);
            if (i == std::string::npos || r < 0 || f > 7) return false;
            put(int(i), r * 8 + f);
            f++;
        }
    }
    side = stm == "b" ? BLACK : WHITE;
    castling = 0;
    for (char c : cast) castling |= c == 'K' ? 1 : c == 'Q' ? 2 : c == 'k' ? 4 : c == 'q' ? 8 : 0;
    ep = (eps.size() == 2 && eps[0] >= 'a' && eps[0] <= 'h') ? (eps[1] - '1') * 8 + (eps[0] - 'a') : -1;
    key = compute_key();
    return popcount(bb[make_piece(WHITE, KING)]) == 1 && popcount(bb[make_piece(BLACK, KING)]) == 1;
}

std::string Position::fen() const {
    std::string out;
    const char *pcs = "PNBRQKpnbrqk";
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int p = board[r * 8 + f];
            if (p == NO_PIECE) empty++;
            else {
                if (empty) out += char('0' + empty);
                empty = 0;
                out += pcs[p];
            }
        }
        if (empty) out += char('0' + empty);
        if (r) out += '/';
    }
    out += side == WHITE ? " w " : " b ";
    std::string c;
    if (castling & 1) c += 'K';
    if (castling & 2) c += 'Q';
    if (castling & 4) c += 'k';
    if (castling & 8) c += 'q';
    out += c.empty() ? "-" : c;
    out += ' ';
    if (ep >= 0) {
        out += char('a' + sq_file(ep));
        out += char('1' + sq_rank(ep));
    } else out += '-';
    out += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
    return out;
}

bool Position::attacked(int s, int by) const {
    U64 o = all();
    if (attacks::pawn[by ^ 1][s] & pieces(by, PAWN)) return true;
    if (attacks::knight[s] & pieces(by, KNIGHT)) return true;
    if (attacks::king[s] & pieces(by, KING)) return true;
    if (attacks::bishop(s, o) & (pieces(by, BISHOP) | pieces(by, QUEEN))) return true;
    if (attacks::rook(s, o) & (pieces(by, ROOK) | pieces(by, QUEEN))) return true;
    return false;
}

/* ---------- move generation ---------- */

static void add_promos(MoveList &l, int from, int to, bool capture, bool queen_only) {
    int base = PROMO | (capture ? CAPTURE : 0);
    l.add(make_move(from, to, base | 3));
    if (queen_only) return;
    l.add(make_move(from, to, base | 0));
    l.add(make_move(from, to, base | 1));
    l.add(make_move(from, to, base | 2));
}

void Position::generate(MoveList &l, bool captures_only) const {
    const int us = side, them = side ^ 1;
    const U64 own = occ[us], enemy = occ[them], o = all(), empty = ~o;
    const int up = us == WHITE ? 8 : -8;
    const U64 last_rank = us == WHITE ? 0xFF00000000000000ULL : 0xFFULL;
    const U64 third_rank = us == WHITE ? 0x0000000000FF0000ULL : 0x0000FF0000000000ULL;

    // pawns
    U64 pawns = pieces(us, PAWN);
    U64 one = (us == WHITE ? pawns << 8 : pawns >> 8) & empty;
    U64 two = (us == WHITE ? (one & third_rank) << 8 : (one & third_rank) >> 8) & empty;
    for (U64 b = one & last_rank; b;) {
        int to = pop_lsb(b);
        add_promos(l, to - up, to, false, captures_only);
    }
    if (!captures_only) {
        for (U64 b = one & ~last_rank; b;) {
            int to = pop_lsb(b);
            l.add(make_move(to - up, to, QUIET));
        }
        for (U64 b = two; b;) {
            int to = pop_lsb(b);
            l.add(make_move(to - 2 * up, to, DOUBLE_PUSH));
        }
    }
    for (U64 b = pawns; b;) {
        int from = pop_lsb(b);
        for (U64 a = attacks::pawn[us][from] & enemy; a;) {
            int to = pop_lsb(a);
            if (bit(to) & last_rank) add_promos(l, from, to, true, false);
            else l.add(make_move(from, to, CAPTURE));
        }
        if (ep >= 0 && (attacks::pawn[us][from] & bit(ep))) l.add(make_move(from, ep, EP_CAPTURE));
    }

    // pieces
    const U64 targets = captures_only ? enemy : ~own;
    auto emit = [&](int from, U64 a) {
        for (a &= targets; a;) {
            int to = pop_lsb(a);
            l.add(make_move(from, to, (enemy & bit(to)) ? CAPTURE : QUIET));
        }
    };
    for (U64 b = pieces(us, KNIGHT); b;) {
        int s = pop_lsb(b);
        emit(s, attacks::knight[s]);
    }
    for (U64 b = pieces(us, BISHOP); b;) {
        int s = pop_lsb(b);
        emit(s, attacks::bishop(s, o));
    }
    for (U64 b = pieces(us, ROOK); b;) {
        int s = pop_lsb(b);
        emit(s, attacks::rook(s, o));
    }
    for (U64 b = pieces(us, QUEEN); b;) {
        int s = pop_lsb(b);
        emit(s, attacks::queen(s, o));
    }
    int k = king_sq(us);
    emit(k, attacks::king[k]);

    // castling: the squares between empty, the king's path not attacked
    if (!captures_only) {
        if (us == WHITE) {
            if ((castling & 1) && !(o & 0x60ULL) && !attacked(4, BLACK) && !attacked(5, BLACK) && !attacked(6, BLACK))
                l.add(make_move(4, 6, KING_CASTLE));
            if ((castling & 2) && !(o & 0x0EULL) && !attacked(4, BLACK) && !attacked(3, BLACK) && !attacked(2, BLACK))
                l.add(make_move(4, 2, QUEEN_CASTLE));
        } else {
            if ((castling & 4) && !(o & 0x6000000000000000ULL) && !attacked(60, WHITE) && !attacked(61, WHITE) &&
                !attacked(62, WHITE))
                l.add(make_move(60, 62, KING_CASTLE));
            if ((castling & 8) && !(o & 0x0E00000000000000ULL) && !attacked(60, WHITE) && !attacked(59, WHITE) &&
                !attacked(58, WHITE))
                l.add(make_move(60, 58, QUEEN_CASTLE));
        }
    }
}

/* ---------- make / unmake ---------- */

bool Position::make(Move m) {
    const int from = move_from(m), to = move_to(m), flag = move_flag(m);
    const int us = side, p = board[from];
    Undo u{m, NO_PIECE, castling, ep, halfmove, key};

    if (ep >= 0) key ^= zobrist::ep[sq_file(ep)];
    ep = -1;

    if (flag == EP_CAPTURE) {
        int cs = us == WHITE ? to - 8 : to + 8;
        u.captured = board[cs];
        remove(cs);
    } else if (flag & CAPTURE) {
        u.captured = board[to];
        remove(to);
    }

    remove(from);
    put(is_promo(m) ? make_piece(us, promo_type(m)) : p, to);

    if (flag == KING_CASTLE) {
        int rf = us == WHITE ? 7 : 63, rt = us == WHITE ? 5 : 61;
        int r = board[rf];
        remove(rf);
        put(r, rt);
    } else if (flag == QUEEN_CASTLE) {
        int rf = us == WHITE ? 0 : 56, rt = us == WHITE ? 3 : 59;
        int r = board[rf];
        remove(rf);
        put(r, rt);
    } else if (flag == DOUBLE_PUSH) {
        ep = us == WHITE ? from + 8 : from - 8;
        key ^= zobrist::ep[sq_file(ep)];
    }

    key ^= zobrist::castling[castling];
    castling &= castle_mask[from] & castle_mask[to];
    key ^= zobrist::castling[castling];

    halfmove = (type_of(p) == PAWN || u.captured != NO_PIECE) ? 0 : halfmove + 1;
    if (us == BLACK) fullmove++;
    side ^= 1;
    key ^= zobrist::side;
    history.push_back(u);

    if (attacked(king_sq(us), side)) {
        unmake();
        return false;
    }
    return true;
}

void Position::unmake() {
    Undo u = history.back();
    history.pop_back();
    side ^= 1;
    const int us = side;
    if (us == BLACK) fullmove--;
    const Move m = u.move;
    const int from = move_from(m), to = move_to(m), flag = move_flag(m);

    int moved = board[to];
    remove(to);
    put(is_promo(m) ? make_piece(us, PAWN) : moved, from);
    if (flag == EP_CAPTURE) put(u.captured, us == WHITE ? to - 8 : to + 8);
    else if (u.captured != NO_PIECE) put(u.captured, to);

    if (flag == KING_CASTLE) {
        int rf = us == WHITE ? 7 : 63, rt = us == WHITE ? 5 : 61;
        int r = board[rt];
        remove(rt);
        put(r, rf);
    } else if (flag == QUEEN_CASTLE) {
        int rf = us == WHITE ? 0 : 56, rt = us == WHITE ? 3 : 59;
        int r = board[rt];
        remove(rt);
        put(r, rf);
    }
    castling = u.castling;
    ep = u.ep;
    halfmove = u.halfmove;
    key = u.key;
}

void Position::make_null() {
    history.push_back(Undo{NO_MOVE, NO_PIECE, castling, ep, halfmove, key});
    if (ep >= 0) key ^= zobrist::ep[sq_file(ep)];
    ep = -1;
    side ^= 1;
    key ^= zobrist::side;
    halfmove++;
}

void Position::unmake_null() {
    Undo u = history.back();
    history.pop_back();
    side ^= 1;
    ep = u.ep;
    halfmove = u.halfmove;
    key = u.key;
}

bool Position::is_repetition() const {
    // positions with the same side to move, back to the last capture or pawn move
    int n = int(history.size());
    for (int i = n - 2; i >= 0 && i >= n - 1 - halfmove; i -= 2)
        if (history[i].key == key) return true;
    return false;
}

bool Position::insufficient_material() const {
    if (pieces(WHITE, PAWN) | pieces(BLACK, PAWN) | pieces(WHITE, ROOK) | pieces(BLACK, ROOK) | pieces(WHITE, QUEEN) |
        pieces(BLACK, QUEEN))
        return false;
    int minors = popcount(pieces(WHITE, KNIGHT) | pieces(BLACK, KNIGHT) | pieces(WHITE, BISHOP) | pieces(BLACK, BISHOP));
    return minors <= 1;
}

std::string Position::move_to_uci(Move m) const {
    if (m == NO_MOVE) return "0000";
    std::string s;
    s += char('a' + sq_file(move_from(m)));
    s += char('1' + sq_rank(move_from(m)));
    s += char('a' + sq_file(move_to(m)));
    s += char('1' + sq_rank(move_to(m)));
    if (is_promo(m)) s += "nbrq"[move_flag(m) & 3];
    return s;
}

Move Position::parse_uci(const std::string &s) {
    MoveList l;
    generate(l);
    for (int i = 0; i < l.size; i++)
        if (move_to_uci(l.moves[i]) == s) return l.moves[i];
    return NO_MOVE;
}

namespace perft_ns {
uint64_t perft(Position &pos, int depth) {
    if (depth == 0) return 1;
    MoveList l;
    pos.generate(l);
    uint64_t n = 0;
    for (int i = 0; i < l.size; i++) {
        if (!pos.make(l.moves[i])) continue;
        n += perft(pos, depth - 1);
        pos.unmake();
    }
    return n;
}
} // namespace perft_ns
