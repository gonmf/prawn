/*
Move generator test for prawn.

perft(N) counts the leaves of the legal move tree N plies deep. The counts for a
handful of standard positions are published and exact, so a single mismatch means
the generator is inventing a move, missing one, or getting legality wrong
somewhere in that tree. It is a better test than playing games: a bug that shows
up once in 200000 nodes will never lose a bait match visibly, but it turns 197281
into 197284 here immediately.

Everything in prawn.c is static, so there are no symbols to link against. Rather
than export any, this file includes the whole translation unit and renames its
main out of the way. The consequence is that prawn.c must NOT also appear on the
compile line, or it is compiled twice and every symbol is duplicated:

    gcc $(CFLAGS) perft-src/main.c fen.c -o perft

Only the three move mask tables are initialised below. The Zobrist tables are
left zeroed, since the hash just_play_*_complex maintains is discarded here, and
that is what lets this run without a zobrist_N.bin in the working directory.

Nothing outside move generation, legality and FEN parsing is exercised: the
hash, the evaluation and the search are all untested by this.

Usage:
    ./perft                   run the suite, exit non-zero if any case fails
    ./perft "<fen>" <depth>   count one position, listing each root move's share
*/

// prawn.c carries a main of its own, which would collide with the one below
#define main prawn_main_unused
#include "../prawn.c"
#undef main

static uint64_t perft(const board_t * board, int depth) {
    play_t valid_plays[218];
    int valid_plays_i = enumerate_legal_plays(valid_plays, board);

    // At one ply from the bottom the move count is the answer
    if (depth <= 1) {
        return (uint64_t)valid_plays_i;
    }

    board_t board_cpy;
    uint64_t total = 0;

    for (int i = 0; i < valid_plays_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t hash = 0;

        if (board_cpy.color == WHITE_COLOR) {
            just_play_white_complex(&board_cpy, &valid_plays[i], &hash);
        } else {
            just_play_black_complex(&board_cpy, &valid_plays[i], &hash);
        }

        total += perft(&board_cpy, depth - 1);
    }

    return total;
}

static void format_play(char * dest, const play_t * play) {
    int i = sprintf(dest, "%c%d%c%d", 'a' + play->from_x, 8 - play->from_y, 'a' + play->to_x, 8 - play->to_y);

    switch (play->promotion_option) {
        case PROMOTION_QUEEN:  dest[i++] = 'q'; break;
        case PROMOTION_KNIGHT: dest[i++] = 'n'; break;
        case PROMOTION_BISHOP: dest[i++] = 'b'; break;
        case PROMOTION_ROOK:   dest[i++] = 'r'; break;
    }

    dest[i] = 0;
}

static void perft_divide(const board_t * board, int depth) {
    play_t valid_plays[218];
    int valid_plays_i = enumerate_legal_plays(valid_plays, board);
    board_t board_cpy;
    uint64_t total = 0;
    char play_str[8];

    for (int i = 0; i < valid_plays_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t hash = 0;

        if (board_cpy.color == WHITE_COLOR) {
            just_play_white_complex(&board_cpy, &valid_plays[i], &hash);
        } else {
            just_play_black_complex(&board_cpy, &valid_plays[i], &hash);
        }

        uint64_t nodes = depth <= 1 ? 1 : perft(&board_cpy, depth - 1);
        total += nodes;

        format_play(play_str, &valid_plays[i]);
        printf("%-6s %llu\n", play_str, (unsigned long long)nodes);
    }

    printf("\n%d moves, %llu nodes\n", valid_plays_i, (unsigned long long)total);
}

// See https://www.chessprogramming.org/Perft_Results
static const struct {
    const char * name;
    const char * fen;
    int depth;
    uint64_t expected;
} cases[] = {
    {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20},
    {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2, 400},
    {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902},
    {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
    {"startpos",   "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48},
    {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, 2039},
    {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},
    {"kiwipete",   "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
    {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
    {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
    {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083},
    {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467},
    {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
    {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
    {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594}
};

int main(int argc, char * argv[]) {
    populate_pawn_capture_masks();
    populate_knight_moves_masks();
    populate_king_moves_masks();

    board_t test_board;
    board_ext_t test_board_ext;

    if (argc == 3) {
        fen_to_board(&test_board, &test_board_ext, argv[1]);
        perft_divide(&test_board, atoi(argv[2]));
        return EXIT_SUCCESS;
    }

    if (argc != 1) {
        printf("Usage:\n");
        printf("  %s                 - run the whole suite\n", argv[0]);
        printf("  %s \"<fen>\" <depth> - count one position, per root move\n", argv[0]);
        return EXIT_FAILURE;
    }

    int failed = 0;
    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fen_to_board(&test_board, &test_board_ext, cases[i].fen);
        uint64_t nodes = perft(&test_board, cases[i].depth);

        if (nodes == cases[i].expected) {
            printf("ok   %-11s depth %d  %llu\n", cases[i].name, cases[i].depth, (unsigned long long)nodes);
        } else {
            printf("FAIL %-11s depth %d  expected %llu, got %llu\n", cases[i].name, cases[i].depth, (unsigned long long)cases[i].expected, (unsigned long long)nodes);
            printf("     %s\n", cases[i].fen);
            failed++;
        }
    }

    gettimeofday(&end, NULL);
    printf("\n%d of %u cases failed, in %.2fs\n", failed, (unsigned int)(sizeof(cases) / sizeof(cases[0])), elapsed_ms(start, end) / 1000.0);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
