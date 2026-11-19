<p align="center"><img src="assets/stockcrab.svg" width="120" alt="StockCrab"></p>

<h1 align="center">StockCrab</h1>

<p align="center">A small chess engine with a character. UCI, 154 KB, plays at the level of Stockfish 17 limited to 2800.</p>

---

StockCrab is a chess engine written from scratch in C++. It is the engine behind the purple crab in the [StockCrab browser extension](https://github.com/upidddd/stockcrab), and it runs in any program that speaks UCI: Arena, Cute Chess, Banksia, En Croissant, lichess-bot and others.

It does not try to be Stockfish. It tries to be small, quick to start, easy to read and easy to give a personality to.

## Against Stockfish

Measured on one machine (Ryzen, 12 threads), both engines on one thread, 64 MB hash, 10 s + 0.1 s per game, 40 games per match from 20 balanced openings with colours swapped. The referee is `tools/match.cpp`.

| Opponent | Result for StockCrab | Elo difference |
|---|---|---|
| Stockfish 17 limited to 2000 | +38 =0 −2 | +512 |
| Stockfish 17 limited to 2400 | +37 =1 −2 | +470 |
| Stockfish 17 limited to 2800 | +18 =3 −19 | −9 (±100) |
| Stockfish 17 at full strength | +0 =0 −20 | not close |

So StockCrab plays about as well as Stockfish 17 set to 2800 at this speed. Stockfish's own Elo scale is calibrated at a slower time control, so take the number as a comparison, not a rating. At full strength Stockfish wins every game: it has fifteen years of work and a neural network trained on billions of positions behind it.

Where StockCrab is ahead:

| | StockCrab 1.0 | Stockfish 17 |
|---|---|---|
| Program size | **154 KB** | 78.7 MB (the network is built in) |
| Browser build | **280 KB**, one file | 7 – 100 MB |
| Memory with a 16 MB hash | **21 MB** | 262 MB |
| Positions per second, one thread | **3.4 million** | 0.55 million (each one evaluated by a network) |
| Personalities | **6, plus 9 dials** | none |
| Strength limit | 200 – 3000 Elo | 1320 – 3190 Elo |
| Comments on the game | yes, if you ask | no |

## The character

The same search plays a different game with a different character: the evaluation is weighted by it.

| Personality | Plays like |
|---|---|
| Balanced | the position, no favourite plan |
| Angry Crab | goes for your king, hates draws, keeps its knights |
| Coward | king safety first, happy with a draw |
| Kamikaze | everything at the king, material is a detail |
| Hoarder | grabs material and holds on to it |
| Positional | slow squeeze: pawn structure and active pieces |

Every dial behind them is a UCI option of its own, so a character can be tuned or built from scratch:

| Option | Range | What it changes |
|---|---|---|
| `Personality` | the six above | loads a character |
| `Aggression` | 0 – 300 | weight of attacking the enemy king |
| `Caution` | 0 – 300 | weight of the own king's shelter |
| `Greed` | 50 – 200 | value of material against everything else |
| `Activity` | 0 – 300 | weight of piece mobility |
| `Pawn Structure` | 0 – 300 | passed, doubled and isolated pawns |
| `Contempt` | −200 – 200 | how much a draw is worth to it, in centipawns |
| `Risk` | 50 – 200 | how hard quiet moves are cut in the search; higher plays sharper |
| `Favourite Piece` | none, knight, bishop, rook, queen | a piece it prefers to keep |
| `Variety` | 0 – 100 | picks among nearly equal moves in the opening, so games differ |
| `Talk` | on / off | the crab comments on the game in `info string crab: …` |

Plus the usual: `Hash`, `MultiPV`, `Move Overhead`, `UCI_LimitStrength` and `UCI_Elo` (200 – 3000; below 3000 it searches shallower and picks among its best lines by how much each gives away).

## How it works

- **Board:** bitboards with magic bitboards for sliding pieces, checked against the standard perft positions (`make test`).
- **Search:** iterative deepening with aspiration windows, principal variation search, transposition table, null move pruning, reverse futility pruning, late move reductions and pruning, check extension, quiescence search with delta pruning. Move ordering: hash move, MVV-LVA, killers, history.
- **Evaluation:** PeSTO's tapered piece-square tables, bishop pair, passed, doubled and isolated pawns, rooks on open files, mobility, king attack and king shelter, each weighted by the character.

About 1 800 lines in `src/`.

## Build

```sh
make            # ./stockcrab, tuned for this machine
make portable   # runs on any x86-64 from 2008 on
make test       # perft
make wasm       # dist/stockcrab.js for browsers (needs Emscripten)
./stockcrab bench
```

The browser build runs as a Web Worker: post UCI commands to it, it posts its output back, line by line, like stockfish.js.

## Licence

MIT. The piece-square tables are PeSTO's by Ronald Friederich, published for free use. By [upidddd](https://github.com/upidddd).
