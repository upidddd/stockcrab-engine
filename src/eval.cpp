// StockCrab: evaluation.
//
// The base is PeSTO's tapered piece-square tables (Ronald Friederich's Rofchade, tuned on millions of games), with
// a middlegame and an endgame value for every piece on every square, blended by how much material is left. On top:
// bishop pair, passed pawns, doubled and isolated pawns, rooks on open files, mobility and king safety. Each group
// is weighted by the character, so the same search plays a different game with a different character.

#include "eval.h"

#include <cstring>

namespace eval {

const int piece_value[6] = {100, 320, 330, 500, 950, 0};

namespace {

Character ch;

const int mg_value[6] = {82, 337, 365, 477, 1025, 0};
const int eg_value[6] = {94, 281, 297, 512, 936, 0};
const int phase_inc[6] = {0, 1, 1, 2, 4, 0};

// tables from White's point of view, a8 first (index ^ 56 for a White piece on our a1 = 0 board)
const int mg_pawn[64] = {
    0,   0,   0,   0,   0,   0,   0,  0,    98,  134, 61,  95,  68,  126, 34,  -11,
    -6,  7,   26,  31,  65,  56,  25,  -20,  -14, 13,  6,   21,  23,  12,  17,  -23,
    -27, -2,  -5,  12,  17,  6,   10,  -25,  -26, -4,  -4,  -10, 3,   3,   33,  -12,
    -35, -1,  -20, -23, -15, 24,  38,  -22,  0,   0,   0,   0,   0,   0,   0,   0};
const int eg_pawn[64] = {
    0,   0,   0,   0,   0,   0,   0,   0,   178, 173, 158, 134, 147, 132, 165, 187,
    94,  100, 85,  67,  56,  53,  82,  84,  32,  24,  13,  5,   -2,  4,   17,  17,
    13,  9,   -3,  -7,  -7,  -8,  3,   -1,  4,   7,   -6,  1,   0,   -5,  -1,  -8,
    13,  8,   8,   10,  13,  0,   2,   -7,  0,   0,   0,   0,   0,   0,   0,   0};
const int mg_knight[64] = {
    -167, -89, -34, -49, 61,  -97, -15, -107, -73, -41, 72,  36,  23,  62,  7,   -17,
    -47,  60,  37,  65,  84,  129, 73,  44,   -9,  17,  19,  53,  37,  69,  18,  22,
    -13,  4,   16,  13,  28,  19,  21,  -8,   -23, -9,  12,  10,  19,  17,  25,  -16,
    -29,  -53, -12, -3,  -1,  18,  -14, -19,  -105, -21, -58, -33, -17, -28, -19, -23};
const int eg_knight[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99, -25, -8,  -25, -2,  -9,  -25, -24, -52,
    -24, -20, 10,  9,   -1,  -9,  -19, -41, -17, 3,   22,  22,  22,  11,  8,   -18,
    -18, -6,  16,  25,  16,  17,  4,   -18, -23, -3,  -1,  15,  10,  -3,  -20, -22,
    -42, -20, -10, -5,  -2,  -20, -23, -44, -29, -51, -23, -15, -22, -18, -50, -64};
const int mg_bishop[64] = {
    -29, 4,   -82, -37, -25, -42, 7,   -8,  -26, 16,  -18, -13, 30,  59,  18,  -47,
    -16, 37,  43,  40,  35,  50,  37,  -2,  -4,  5,   19,  50,  37,  37,  7,   -2,
    -6,  13,  13,  26,  34,  12,  10,  4,   0,   15,  15,  15,  14,  27,  18,  10,
    4,   15,  16,  0,   7,   21,  33,  1,   -33, -3,  -14, -21, -13, -12, -39, -21};
const int eg_bishop[64] = {
    -14, -21, -11, -8,  -7,  -9,  -17, -24, -8,  -4,  7,   -12, -3,  -13, -4,  -14,
    2,   -8,  0,   -1,  -2,  6,   0,   4,   -3,  9,   12,  9,   14,  10,  3,   2,
    -6,  3,   13,  19,  7,   10,  -3,  -9,  -12, -3,  8,   10,  13,  3,   -7,  -15,
    -14, -18, -7,  -1,  4,   -9,  -15, -27, -23, -9,  -23, -5,  -9,  -16, -5,  -17};
const int mg_rook[64] = {
    32,  42,  32,  51,  63,  9,   31,  43,  27,  32,  58,  62,  80,  67,  26,  44,
    -5,  19,  26,  36,  17,  45,  61,  16,  -24, -11, 7,   26,  24,  35,  -8,  -20,
    -36, -26, -12, -1,  9,   -7,  6,   -23, -45, -25, -16, -17, 3,   0,   -5,  -33,
    -44, -16, -20, -9,  -1,  11,  -6,  -71, -19, -13, 1,   17,  16,  7,   -37, -26};
const int eg_rook[64] = {
    13, 10, 18, 15, 12, 12,  8,   5,   11, 13, 13, 11, -3, 3,   8,   3,
    7,  7,  7,  5,  4,  -3,  -5,  -3,  4,  3,  13, 1,  2,  1,   -1,  2,
    3,  5,  8,  4,  -5, -6,  -8,  -11, -4, 0,  -5, -1, -7, -12, -8,  -16,
    -6, -6, 0,  2,  -9, -9,  -11, -3,  -9, 2,  3,  -1, -5, -13, 4,   -20};
const int mg_queen[64] = {
    -28, 0,   29,  12,  59,  44,  43,  45,  -24, -39, -5,  1,   -16, 57,  28,  54,
    -13, -17, 7,   8,   29,  56,  47,  57,  -27, -27, -16, -16, -1,  17,  -2,  1,
    -9,  -26, -9,  -10, -2,  -4,  3,   -3,  -14, 2,   -11, -2,  -5,  2,   14,  5,
    -35, -8,  11,  2,   8,   15,  -3,  1,   -1,  -18, -9,  10,  -15, -25, -31, -50};
const int eg_queen[64] = {
    -9,  22,  22,  27,  27,  19,  10,  20,  -17, 20,  32,  41,  58,  25,  30,  0,
    -20, 6,   9,   49,  47,  35,  19,  9,   3,   22,  24,  45,  57,  40,  57,  36,
    -18, 28,  19,  47,  31,  34,  39,  23,  -16, -27, 15,  6,   9,   17,  10,  5,
    -22, -23, -30, -16, -16, -23, -36, -32, -33, -28, -22, -43, -5,  -32, -20, -41};
const int mg_king[64] = {
    -65, 23,  16,  -15, -56, -34, 2,   13,  29,  -1,  -20, -7,  -8,  -4,  -38, -29,
    -9,  24,  2,   -16, -20, 6,   22,  -22, -17, -20, -12, -27, -30, -25, -14, -36,
    -49, -1,  -27, -39, -46, -44, -33, -51, -14, -14, -22, -46, -44, -30, -15, -27,
    1,   7,   -8,  -64, -43, -16, 9,   8,   -15, 36,  12,  -54, 8,   -28, 24,  14};
const int eg_king[64] = {
    -74, -35, -18, -18, -11, 15,  4,   -17, -12, 17,  14,  17,  17,  38,  23,  11,
    10,  17,  23,  15,  20,  45,  44,  13,  -8,  22,  24,  27,  26,  33,  26,  3,
    -18, -4,  21,  24,  27,  23,  9,   -11, -19, -3,  11,  21,  23,  16,  7,   -9,
    -27, -11, 4,   13,  14,  4,   -5,  -17, -53, -34, -21, -11, -28, -14, -24, -43};

const int *mg_tables[6] = {mg_pawn, mg_knight, mg_bishop, mg_rook, mg_queen, mg_king};
const int *eg_tables[6] = {eg_pawn, eg_knight, eg_bishop, eg_rook, eg_queen, eg_king};

// [piece 0..11][square]: material + table, scaled by the character's greed
int mg_pst[12][64], eg_pst[12][64];

U64 file_mask[8], adjacent_files[8], passed_mask[2][64], king_zone[64];

const int passed_mg[8] = {0, 5, 10, 15, 30, 55, 90, 0};
const int passed_eg[8] = {0, 10, 20, 35, 60, 100, 160, 0};

void build() {
    for (int t = 0; t < 6; t++)
        for (int s = 0; s < 64; s++) {
            int mg_mat = mg_value[t] * ch.greed / 100, eg_mat = eg_value[t] * ch.greed / 100;
            int fav = (t == ch.favourite) ? 25 : 0;
            mg_pst[make_piece(WHITE, t)][s] = mg_mat + fav + mg_tables[t][s ^ 56];
            eg_pst[make_piece(WHITE, t)][s] = eg_mat + fav + eg_tables[t][s ^ 56];
            mg_pst[make_piece(BLACK, t)][s] = mg_mat + fav + mg_tables[t][s];
            eg_pst[make_piece(BLACK, t)][s] = eg_mat + fav + eg_tables[t][s];
        }
}

struct Init {
    Init() {
        for (int f = 0; f < 8; f++) {
            file_mask[f] = 0x0101010101010101ULL << f;
        }
        for (int f = 0; f < 8; f++) adjacent_files[f] = (f > 0 ? file_mask[f - 1] : 0) | (f < 7 ? file_mask[f + 1] : 0);
        for (int s = 0; s < 64; s++) {
            int f = sq_file(s), r = sq_rank(s);
            U64 span = file_mask[f] | adjacent_files[f];
            U64 ahead_w = r < 7 ? ~0ULL << (8 * (r + 1)) : 0;
            U64 ahead_b = r > 0 ? ~0ULL >> (8 * (8 - r)) : 0;
            passed_mask[WHITE][s] = span & ahead_w;
            passed_mask[BLACK][s] = span & ahead_b;
            U64 z = bit(s);
            for (int dr = -1; dr <= 1; dr++)
                for (int df = -1; df <= 1; df++) {
                    int ff = f + df, rr = r + dr;
                    if (ff >= 0 && ff < 8 && rr >= 0 && rr < 8) z |= bit(rr * 8 + ff);
                }
            king_zone[s] = z;
        }
        build();
    }
} init_eval;

// weights of attacking units near the enemy king, by number of attackers
const int attack_weight[8] = {0, 0, 50, 75, 88, 94, 97, 99};
const int attacker_value[6] = {0, 20, 20, 40, 80, 0};

} // namespace

void set_character(const Character &c) {
    ch = c;
    build();
}
const Character &character() { return ch; }

const char *preset_names() { return "Balanced var Angry Crab var Coward var Kamikaze var Hoarder var Positional"; }

bool load_preset(const std::string &name, Character &c) {
    c = Character{};
    c.name = name;
    if (name == "Balanced") return true;
    if (name == "Angry Crab") { // attacks the king, hates draws, keeps its knights
        c.aggression = 170; c.caution = 85; c.activity = 120; c.contempt = 40; c.favourite = KNIGHT; c.risk = 120;
        return true;
    }
    if (name == "Coward") { // king first, happy with a draw, trades into safety
        c.aggression = 60; c.caution = 180; c.greed = 110; c.contempt = -30; c.risk = 80;
        return true;
    }
    if (name == "Kamikaze") { // everything at the king, material is a detail
        c.aggression = 260; c.caution = 50; c.greed = 75; c.activity = 140; c.contempt = 80; c.risk = 150;
        return true;
    }
    if (name == "Hoarder") { // grabs material and holds on to it
        c.greed = 130; c.aggression = 80; c.activity = 80; c.contempt = 10;
        return true;
    }
    if (name == "Positional") { // slow squeeze: structure and activity over adventures
        c.pawns = 150; c.activity = 130; c.aggression = 80; c.caution = 120; c.risk = 85;
        return true;
    }
    return false;
}

int evaluate(const Position &pos) {
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    const U64 o = pos.all();

    for (int p = 0; p < 12; p++) {
        int c = color_of(p), t = type_of(p);
        for (U64 b = pos.bb[p]; b;) {
            int s = pop_lsb(b);
            mg[c] += mg_pst[p][s];
            eg[c] += eg_pst[p][s];
            phase += phase_inc[t];
        }
    }

    for (int c = 0; c < 2; c++) {
        const int them = c ^ 1;
        const U64 own_pawns = pos.pieces(c, PAWN), their_pawns = pos.pieces(them, PAWN);
        int smg = 0, seg = 0;

        // bishop pair
        if (popcount(pos.pieces(c, BISHOP)) >= 2) {
            smg += 30;
            seg += 50;
        }

        // pawn structure
        int pmg = 0, peg = 0;
        for (U64 b = own_pawns; b;) {
            int s = pop_lsb(b), f = sq_file(s);
            int rel_rank = c == WHITE ? sq_rank(s) : 7 - sq_rank(s);
            if (!(passed_mask[c][s] & their_pawns)) {
                pmg += passed_mg[rel_rank];
                peg += passed_eg[rel_rank];
                // a passer whose path is blocked is worth less
                int front = c == WHITE ? s + 8 : s - 8;
                if (front >= 0 && front < 64 && (o & bit(front))) peg -= passed_eg[rel_rank] / 3;
            }
            if (!(adjacent_files[f] & own_pawns)) {
                pmg -= 10;
                peg -= 15;
            }
            if (popcount(file_mask[f] & own_pawns) > 1) {
                pmg -= 8;
                peg -= 20;
            }
        }
        smg += pmg * ch.pawns / 100;
        seg += peg * ch.pawns / 100;

        // rooks on open and half-open files
        for (U64 b = pos.pieces(c, ROOK); b;) {
            int f = sq_file(pop_lsb(b));
            if (!(file_mask[f] & own_pawns)) {
                bool open = !(file_mask[f] & their_pawns);
                smg += open ? 25 : 12;
                seg += open ? 10 : 5;
            }
        }

        // mobility (squares not attacked by enemy pawns) and attacks near the enemy king
        U64 enemy_pawn_att = 0;
        for (U64 b = their_pawns; b;) enemy_pawn_att |= attacks::pawn[them][pop_lsb(b)];
        const U64 safe = ~pos.occ[c] & ~enemy_pawn_att;
        const U64 zone = king_zone[pos.king_sq(them)];
        int mob = 0, attackers = 0, attack_value = 0;
        auto piece = [&](int t, U64 a) {
            mob += popcount(a & safe) * (t == QUEEN ? 1 : t == ROOK ? 2 : 4);
            if (a & zone) {
                attackers++;
                attack_value += attacker_value[t] * popcount(a & zone);
            }
        };
        for (U64 b = pos.pieces(c, KNIGHT); b;) piece(KNIGHT, attacks::knight[pop_lsb(b)]);
        for (U64 b = pos.pieces(c, BISHOP); b;) piece(BISHOP, attacks::bishop(pop_lsb(b), o));
        for (U64 b = pos.pieces(c, ROOK); b;) piece(ROOK, attacks::rook(pop_lsb(b), o));
        for (U64 b = pos.pieces(c, QUEEN); b;) piece(QUEEN, attacks::queen(pop_lsb(b), o));
        smg += mob * ch.activity / 100;
        seg += mob * ch.activity / 100;
        if (pos.pieces(c, QUEEN)) // an attack without a queen rarely lands
            smg += attack_value * attack_weight[attackers > 7 ? 7 : attackers] / 100 * ch.aggression / 100;

        // own king: pawn shield in the middlegame
        int k = pos.king_sq(c);
        int shield = popcount(king_zone[k] & own_pawns);
        int open_near = 0;
        for (int df = -1; df <= 1; df++) {
            int f = sq_file(k) + df;
            if (f >= 0 && f < 8 && !(file_mask[f] & own_pawns)) open_near++;
        }
        smg += (shield * 12 - open_near * 18) * ch.caution / 100;

        mg[c] += smg;
        eg[c] += seg;
    }

    if (phase > 24) phase = 24;
    int us = pos.side;
    int mg_score = mg[us] - mg[us ^ 1], eg_score = eg[us] - eg[us ^ 1];
    int score = (mg_score * phase + eg_score * (24 - phase)) / 24;
    return score + 10; // tempo: it is our move
}

} // namespace eval
