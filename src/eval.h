// StockCrab: evaluation and the engine's character.
#pragma once

#include <string>

#include "board.h"

// How StockCrab wants to play. Every value is a percentage around 100 (or a plain number where noted), so
// 100 everywhere is the plain, balanced engine.
struct Character {
    std::string name = "Balanced";
    int aggression = 100;   // weight of attacking the enemy king
    int caution = 100;      // weight of the own king's safety
    int greed = 100;        // weight of material against everything else
    int activity = 100;     // weight of piece mobility
    int pawns = 100;        // weight of pawn structure and passed pawns
    int contempt = 0;       // centipawns: how much a draw is worth less than equal (positive avoids draws)
    int favourite = -1;     // a piece type it likes to keep (KNIGHT..QUEEN), -1 none
    int risk = 100;         // >100 searches sharp, forcing lines deeper and prunes quiet ones harder
};

namespace eval {
void set_character(const Character &c);
const Character &character();
// the presets: Balanced, Angry Crab, Coward, Kamikaze, Hoarder, Positional
bool load_preset(const std::string &name, Character &out);
const char *preset_names();

// static evaluation from the side to move's point of view, in centipawns
int evaluate(const Position &pos);
extern const int piece_value[6];
} // namespace eval
