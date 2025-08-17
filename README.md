# prawn 🦐

A toy chess playing program using minimax with bitmaps, alpha-beta pruning and transpositions detection.

It supports self-play and playing versus a human player via text or the UCI protocol.

For playing via UCI I recommend [Shreder](https://www.shredderchess.com/download.html) as graphical interface.

For performance testing against another program I use `cutechess`:

```sh
./cutechess-cli -engine cmd=./prawn proto=uci -engine cmd=./prawn-alt proto=uci -games 10 -each tc=40/60 -debug
```

In terms of strength it is about 1900 Lichess ELO with the default settings.
