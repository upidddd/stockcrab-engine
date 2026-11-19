// StockCrab: the board, move generation and make/unmake.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

using U64 = uint64_t;

enum Color : int { WHITE, BLACK };
enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_TYPE };
// piece = color * 6 + type
enum : int { NO_PIECE = 12 };

inline int make_piece(int c, int t) { return c * 6 + t; }
inline int type_of(int p) { return p % 6; }
inline int color_of(int p) { return p / 6; }

// squares: a1 = 0, b1 = 1, ..., h8 = 63
inline int sq_file(int s) { return s & 7; }
inline int sq_rank(int s) { return s >> 3; }
inline U64 bit(int s) { return 1ULL << s; }
inline int lsb(U64 b) { return __builtin_ctzll(b); }
inline int pop_lsb(U64 &b) {
    int s = lsb(b);
    b &= b - 1;
    return s;
}
inline int popcount(U64 b) { return __builtin_popcountll(b); }

// A move in 16 bits: from (6) | to (6) | flags (4).
using Move = uint16_t;
enum MoveFlag : int {
    QUIET = 0, DOUBLE_PUSH = 1, KING_CASTLE = 2, QUEEN_CASTLE = 3, CAPTURE = 4, EP_CAPTURE = 5,
    PROMO = 8, // + 0 knight, 1 bishop, 2 rook, 3 queen; + 4 when it also captures
};
inline Move make_move(int from, int to, int flag) { return Move(from | (to << 6) | (flag << 12)); }
inline int move_from(Move m) { return m & 63; }
inline int move_to(Move m) { return (m >> 6) & 63; }
inline int move_flag(Move m) { return m >> 12; }
inline bool is_capture(Move m) { return move_flag(m) & CAPTURE; }
inline bool is_promo(Move m) { return move_flag(m) & PROMO; }
inline int promo_type(Move m) { return KNIGHT + (move_flag(m) & 3); }
constexpr Move NO_MOVE = 0;

struct MoveList {
    Move moves[256];
    int size = 0;
    void add(Move m) { moves[size++] = m; }
};

// attack tables
namespace attacks {
void init();
extern U64 pawn[2][64], knight[64], king[64];
U64 bishop(int s, U64 occ);
U64 rook(int s, U64 occ);
inline U64 queen(int s, U64 occ) { return bishop(s, occ) | rook(s, occ); }
} // namespace attacks

namespace zobrist {
extern U64 piece[12][64], castling[16], ep[8], side;
}

struct Undo {
    Move move;
    int captured;
    int castling;
    int ep;
    int halfmove;
    U64 key;
};

class Position {
public:
    U64 bb[12] = {};
    U64 occ[2] = {};
    int board[64];
    int side = WHITE;
    int castling = 0; // 1 white O-O, 2 white O-O-O, 4 black O-O, 8 black O-O-O
    int ep = -1;      // en passant target square, or -1
    int halfmove = 0;
    int fullmove = 1;
    U64 key = 0;
    std::vector<Undo> history;

    bool set_fen(const std::string &fen);
    std::string fen() const;

    U64 all() const { return occ[WHITE] | occ[BLACK]; }
    U64 pieces(int c, int t) const { return bb[make_piece(c, t)]; }
    int king_sq(int c) const { return lsb(bb[make_piece(c, KING)]); }

    bool attacked(int s, int by) const;
    bool in_check() const { return attacked(king_sq(side), side ^ 1); }

    // pseudo-legal moves; captures_only for the quiescence search
    void generate(MoveList &list, bool captures_only = false) const;
    // makes the move; returns false (and takes it back) if it leaves the own king in check
    bool make(Move m);
    void unmake();
    void make_null();
    void unmake_null();

    bool is_repetition() const;
    bool insufficient_material() const;
    U64 compute_key() const;

    std::string move_to_uci(Move m) const;
    Move parse_uci(const std::string &s);

private:
    void put(int p, int s);
    void remove(int s);
};

namespace perft_ns {
uint64_t perft(Position &pos, int depth);
}
