# prawn 🦐

A toy chess playing program using minimax with bitmaps, alpha-beta pruning and transpositions detection.

It has a text interface, supports self-play, and playing versus a human player via text or the UCI protocol.

For playing via UCI I recommend [Shredder](https://www.shredderchess.com/download.html) as graphical interface.

For regression testing, playing against other versions of itself, a separate tool called `two-uci` is available:

```sh
./two-uci "./prawn-test"  "./prawn-baseline"
```

In terms of strength it is about 1900 Lichess ELO with the default settings.
