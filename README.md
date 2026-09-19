# prawn 🦐

A toy chess playing program using minimax with bitmaps, alpha-beta pruning and
transpositions detection.

**prawn** understands FEN game notation, moves in long algebraic notation, and
it's own format of simple opening books. It has a text interface, with which it
supports self-play and playing versus human players, plus it supports external
graphical user intrfaces via the UCI chess protocol.

For regression testing, playing against other versions of itself, a separate
tool called **bait** is available:

```sh
./bait "./prawn-test"  "./prawn-baseline"
```

**bait** is tailored specifically for **prawn**, and **prawn** is currently
deterministic, meaning two versions playing against each other, repeatedly,
will always arrive at the same result. The only exception is when using the
opening book. This means that when regression testing with the opening book
enabled, it is never necessary to run it for longer than the leaf variety of
the opening book.

Because of this, **prawn** can be asked to convert the opening book into a list
of positions in FEN format, and **bait** can then be asked to cycle through all
positions, two games for each (alternating colors), instead of randomly picking
new openings every time.

To do this run

```sh
./prawn --convert-ob-at=4 > bait.input

./bait "./prawn-test" "./prawn-baseline" --collection=bait.input
```

Regression testing from a given collection automatically disables the openings
book.

## Move generator tests

**perft** counts the leaves of the legal move tree to a given depth. The counts
for a set of standard positions are known exactly, so any mismatch means the
generator is missing a move, inventing one, or getting legality wrong somewhere
in that tree.

```sh
make test

./perft "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" 3
```

## Zobrist tables

**prawn** does not generate its Zobrist keys at startup; it reads them from a
`zobrist_N.bin` file in the working directory, where `N` is the number of keys
it needs: one per piece per square, one for the side to move, one per en passant
file and one per castling right, currently 781 in all. A separate tool called
**zobrist-gen** produces these files:

```sh
make zobrist-gen

./zobrist-gen
```

Keys are drawn from `/dev/urandom`, constrained to have exactly 32 set bits
each and to be at least 16 bits apart from every other key. Whole tables are
drawn repeatedly and the one with the most even distribution of set bits across
the 64 bit positions is kept, either until ENTER is pressed or until the time
budget given by `--seconds` runs out.

Use `--entries=N` to ask for a table of a size other than the default.
