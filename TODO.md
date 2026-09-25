# TODO

Things planned for development, in no specific order.

## General

- Refactor program into separate files for opening books, zobrist hashing, minimax search, etc.
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

- Late move reductions/pruning
- Principal variation search
- Aspiration windows
- Reverse futility / static null move
- Futility pruning
- Razoring
- Internal iterative deepening for nodes with no TT move
- SEE pruning in the main search instead of just in qsearch
- Search extensions - check, recapture, singular extensions, mate-distance pruning
- Check generation for quiet mating nets
- MVV-LVA, countermove, continuation history, SEE-based capture sort
- Node lazy generation

## Performance

- Magic bitboards for sliding pieces
- Incremental occupancy
- New board structure side-by-side with bitmap for fast piece identification
- Move undo in board structure
- Pawn hash table, eval cache, lazy eval, incremental eval

## Board eval

- Pawn structure analysis
- More phased PST besides just the king's
- Weight tuning

## Things that will not be implemented

- Multi-threading
- SSE or other such optimizations that would increase or restrict hardware requirements
- NN-based machine learning
- TT prefetching
