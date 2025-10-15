# prawn 🦐

A toy chess playing program using minimax with bitmaps, alpha-beta pruning and
transpositions detection.

It has a text interface, supports self-play, and playing versus a human player
via text or the UCI chess protocol. It does not have a graphical interface yet.

For regression testing, playing against other versions of itself, a separate
tool called `bait` is available:

```sh
./bait "./prawn-test"  "./prawn-baseline"
```

`bait` is tailored specifically for `prawn`, and `prawn` is currently
deterministic, meaning two versions playing against each other, repeatedly,
will always arrive at the same result. The only exception is when using the
opening book. This means that when regression testing with the opening book
enabled, it is never necessary to run it for longer than the leaf variety of
the opening book.

Because of this, `prawn` can be asked to convert the opening book into a list
of positions in FEN format, and `bait` can then be asked to cycle through all
positions, two games for each (alternating colors), instead of randomly picking
new openings every time.

To do this run

```sh
./prawn --convert-ob-at=4 > bait.input

./bait "./prawn-test" "./prawn-baseline" --collection=bait.input
```

In terms of strength it is about 2000 Lichess ELO with the default settings.
