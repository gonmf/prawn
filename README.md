# prawn 🦐

[![lichess-bullet](https://lichess-shield.vercel.app/api?username=prawn_bot&format=bullet)](https://lichess.org/@/prawn_bot/perf/bullet)
[![lichess-blitz](https://lichess-shield.vercel.app/api?username=prawn_bot&format=blitz)](https://lichess.org/@/prawn_bot/perf/blitz)
[![lichess-rapid](https://lichess-shield.vercel.app/api?username=prawn_bot&format=rapid)](https://lichess.org/@/prawn_bot/perf/rapid)

A small but strong single-threaded chess program. [Challenge it to a game on lichess](https://lichess.org/@/prawn_bot).

**prawn** performs negamax with alpha-beta pruning and iterative deepening (for timed play) that then switches to a quiescence search that resolves captures so the evaluation is never taken in the middle of a trade. On the quiescence search SEE and delta pruning are performed. On the main search it does null-move pruning, late move reductions and check extensions, and sorts plays by transposition table hit first, then captures, then quiet plays by killer move and the history heuristic. The transposition table uses Zobrist hashing and incremental hash updates. Move generation uses precomputed attack masks and bitwise operations. Sliding pieces use magic bitboards.

Evaluation adapts between stages of the game. It uses material value plus PST and pawn structure analysis (passed pawns scaled by rank and by whether they are blocked, defended or beyond the reach of the enemy king, along with doubled, isolated and backward pawns, all kept in a pawn hash), piece mobility, king safety, and sole king mop-up. King safety counts the pawns sheltering a king against possible attackers.

It understands FEN notation, moves in long algebraic notation, and its own format of simple opening books. It has a text interface supporting self-play and play against a human, and it speaks the UCI protocol for external graphical interfaces and timed play. It does not yet ponder. Time is split between a soft limit, which decides whether another iteration is affordable from what the last ones cost, and a hard one that stops a search in progress; positions where the best play changes or the score drops get more time.

Check [TODO.md](TODO.md) for all missing and planned features.

## Building

```sh
make
```

Builds `prawn`, plus the `bait`, `perft`, `zobrist-gen` and `magic-gen` tools. A C99 compiler
is all that is needed.

## Running

```sh
./prawn                 # UCI mode, for a graphical interface
./prawn --text          # play at the terminal
./prawn --self          # watch it play itself
```

| option | |
|---|---|
| `--from-fen FEN` | start from a position |
| `--no-book` | do not use the opening book |
| `--convert-ob-at=N` | print the opening book as a FEN list, for positions at depth N |
| `--uci` | UCI interface mode, the default |
| `--text` | text interface |
| `--self` | play against itself |
| `--extend-uci` | reply `loss`/`draw` instead of a move when the game is over, which is what **bait** expects. Not valid UCI |
| `--help`, `-h` | this list |

`openings.txt`, `zobrist_781.bin`, `magic_rook.bin` and `magic_bishop.bin` are
looked for next to the executable first and in the working directory second.
Only the opening file is optional.

In UCI mode it reports `info depth`, `score`, `nodes`, `nps`, `time` and the best
play for every finished iteration, and writes a log of the session to a
`prawn_<timestamp>_<id>.log` file.

## Regression testing

**bait** plays two builds against each other:

```sh
./bait "./prawn-test" "./prawn-baseline"
```

Without an opening book `prawn` is deterministic, so two builds playing each other
repeatedly always reach the same result. That makes a fixed list of starting
positions more useful than random openings. `prawn` can print one from its own
opening book:

```sh
./prawn --convert-ob-at=4 > bait.input

./bait "./prawn-test" "./prawn-baseline" --collection=bait.input
```

Each position is played twice, with colours reversed. Using a collection disables
the opening book.

## Move generator tests

**perft** counts the leaves of the legal move tree to a given depth. The counts
for a set of standard positions are known exactly, so any mismatch means the
generator is missing a move, inventing one, or getting legality wrong somewhere
in that tree.

```sh
make test
```

A single position can be counted on its own, listing how many leaves each play at
the root accounts for, which narrows a wrong total down to the play causing it:

```sh
./perft "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 3
```

## Zobrist tables

`prawn` does not generate its Zobrist keys at startup; it reads them from a
`zobrist_N.bin` file, where `N` is the number of keys it needs: one per piece per
square, one for the side to move, one per en passant file and one per castling
right, currently 781 in all. `zobrist_781.bin` is in the repository, so this only
matters if you want to make your own. A separate tool produces them:

```sh
make zobrist-gen

./zobrist-gen
```

Keys are drawn from `/dev/urandom`, constrained to have exactly 32 set bits each
and to be at least 16 bits apart from every other key. Whole tables are drawn
repeatedly and the one with the most even distribution of set bits across the 64
bit positions is kept, either until ENTER is pressed or until the time budget
given by `--seconds` runs out.

Use `--entries=N` to ask for a table of a size other than the default.

## Magic bitboards

Sliding piece attacks are table lookups. For each square only the squares along
its rays can block it, and a magic constant multiplies that blocker pattern
into an index — the scattered relevant bits land packed together in the high
bits of the product. The following program finds such constants and stores them
in files `magic_rook.bin` and `magic_bishop.bin`.

```sh
make magic-gen

./magic-gen
```

The constants are used at startup to rebuild the magic bitboards tables. On a
different architecture you may need to rerun `magic-gen` for `prawn` to work.
