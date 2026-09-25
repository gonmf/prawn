# prawn 🦐

[![lichess-bullet](https://lichess-shield.vercel.app/api?username=prawn_bot&format=bullet)](https://lichess.org/@/prawn_bot/perf/bullet)
[![lichess-blitz](https://lichess-shield.vercel.app/api?username=prawn_bot&format=blitz)](https://lichess.org/@/prawn_bot/perf/blitz)
[![lichess-rapid](https://lichess-shield.vercel.app/api?username=prawn_bot&format=rapid)](https://lichess.org/@/prawn_bot/perf/rapid)
[![lichess-classical](https://lichess-shield.vercel.app/api?username=prawn_bot&format=classical)](https://lichess.org/@/prawn_bot/perf/classical)

A small but strong single-threaded chess program. [Challenge it to a game on lichess](https://lichess.org/@/prawn_bot).

**prawn** performs alpha-beta pruning with iterative deepening for timed play with quiescence search at the leafs, with delta, SEE and null-move pruning. Move generation uses precomputed attack masks and bitwise operations. A transposition table is also used with Zobrist hashing, incremental hash updates. It uses a simple material evaluator with mid/endgame tables and some nudges for endgame play (no endgame tables yet).

It understands FEN notation, moves in long algebraic notation, and its own format of simple opening books. It has a text interface supporting self-play and play against a human, and it speaks the UCI protocol for external graphical interfaces and timed play.

Check [TODO.md](TODO.md) for all missing and planned features.


## Building

```sh
make
```

Builds `prawn`, plus the `bait` and `perft` test tools. A C99 compiler is all that
is needed; the Makefile uses `-march=native`.

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

`openings.txt` and `zobrist_781.bin` are looked for next to the executable first
and in the working directory second, so prawn can be started from anywhere. It
runs without an opening book, with a warning.

In UCI mode it reports `info depth`, `score`, `nodes`, `nps`, `time` and the best
play for every finished iteration, and writes a log of the session to a
`prawn_<timestamp>_<id>.log` file.

## Regression testing

**bait** plays two builds against each other:

```sh
./bait "./prawn-test" "./prawn-baseline"
```

Without an opening book prawn is deterministic, so two builds playing each other
repeatedly always reach the same result. That makes a fixed list of starting
positions more useful than random openings. prawn can print one from its own
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

It reaches prawn's internals by including `prawn.c` whole, so it needs no Zobrist
file and can run from anywhere.

## Zobrist tables

**prawn** does not generate its Zobrist keys at startup; it reads them from a
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
