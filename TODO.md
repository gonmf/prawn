# TODO

Things planned for development, in no specific order.

## General

- Implement end tables (Syzygy, not Gaviota)
- Implement proper opening books
- Contempt / draw aversion
- PGN support
- Better testing harness for both performance and correctness
- Option to disable logging

## UCI and text interface

- Support UCI stop and go infinite
- Pondering
- MultiPV options, better UCI info
- Support SAN instead of only long algebraic notation
- In text interface use SAN by default

## Search

- Principal variation search
- Aspiration windows
- Reverse futility / static null move
- Futility pruning
- Razoring
- Internal iterative deepening for nodes with no TT move
- SEE pruning in the main search instead of just in qsearch
- Search extensions - recapture, singular extensions, mate-distance pruning
- Check generation for quiet mating nets
- MVV-LVA, countermove, continuation history, SEE-based capture sort
- Node lazy generation

## Performance

- Magic bitboards for sliding pieces
- Incremental occupancy
- New board structure side-by-side with bitmap for fast piece identification
- Move undo in board structure
- Eval cache, incremental eval
- Lazy eval

## Board eval

- Safe checks
- Pawn storms
- Squares by the king defended by nothing but the king
- Mobility that knows a piece is trapped rather than merely short of squares
- Candidate passers
- Blockade quality
- Rook behind a passed pawn
- King opposition, or a KPK bitbase in place of guessing at it
- Doubled pawns counted worse when also isolated
- Bad bishop
- Knight outposts
- More phased PST besides just the king's
- Weight tuning

## Things that will not be implemented

- Multi-threading
- SSE or other such optimizations that would increase or restrict hardware requirements
- NN-based machine learning
- TT prefetching
