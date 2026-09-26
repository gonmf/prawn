#ifndef __COMMON_H_
#define __COMMON_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#define PROGRAM_VERSION "1.2"

typedef struct {
    uint64_t white_pawns;
    uint64_t black_pawns;
    uint64_t white_knights;
    uint64_t black_knights;
    uint64_t white_bishops;
    uint64_t black_bishops;
    uint64_t white_rooks;
    uint64_t black_rooks;
    uint64_t white_queens;
    uint64_t black_queens;
    uint64_t white_kings;
    uint64_t black_kings;
    uint64_t white_mask; // equivalent to the OR of the above
    uint64_t black_mask; // equivalent to the OR of the above
    char white_left_castling;
    char white_right_castling;
    char black_left_castling;
    char black_right_castling;
    char color;
    char en_passant_x;
    unsigned char halfmoves;
} board_t;

typedef struct {
    char from_x;
    char from_y;
    char to_x;
    char to_y;
} play_short_t;

typedef struct {
    char from_x;
    char from_y;
    char to_x;
    char to_y;
    char promotion_option;
} play_t;

#define MAX_GAME_PLAYS 512

typedef struct {
    // of the position before each play, for repetition detection
    int64_t past_hashes[MAX_GAME_PLAYS];
    play_t past_plays[MAX_GAME_PLAYS];
    unsigned int past_plays_count;
    unsigned int fullmoves;
    char last_play_x;
    char last_play_y;
} board_ext_t;

// draft is how many plays were still left to search below the node when the score was produced,
// so a shallower entry can be recognised and its score refused; quiescence entries store 0.
// age is the number of the search that wrote it, which is what lets an entry left over from an
// earlier move be picked as the one to overwrite. Sixteen bytes.
typedef struct {
    int64_t hash;
    int32_t score_w_type;
    int16_t best_play;
    int8_t draft;
    uint8_t age;
} hash_table_entry_t;

// Max search depth only reachable without time limits.
#define MAX_SEARCH_DEPTH 64
#ifndef DEFAULT_SEARCH_DEPTH
#define DEFAULT_SEARCH_DEPTH 7
#endif
#ifndef QUIESCENCE_EXTRA_DEPTH
#define QUIESCENCE_EXTRA_DEPTH 6
#endif
#define MAX_TOTAL_SEARCH_DEPTH (MAX_SEARCH_DEPTH + QUIESCENCE_EXTRA_DEPTH)
#define HASH_TABLE_BUCKET 4

#ifndef NULL_MOVE_REDUCTION
#define NULL_MOVE_REDUCTION 2
#endif

// Quiet plays searched at full depth before reductions start.
#ifndef LMR_MIN_PLAYS
#define LMR_MIN_PLAYS 3
#endif

// How far past the soft limit an iteration already under way is allowed to run, and the share of
// the clock that overrun is never allowed to exceed.
#define TIME_HARD_MULTIPLIER 3
#define TIME_HARD_CLOCK_SHARE 8

// Held back from the clock for the move to reach the other end.
#define TIME_MOVE_OVERHEAD_MS 100

// Added to the soft limit, as a percentage, while the search has not settled on an answer.
#define TIME_UNSTABLE_EXTRA 50
#define TIME_DROP_THRESHOLD 50

// Growth from one iteration to the next, as a percentage, measured but pulled towards a prior.
#ifndef TIME_RATIO_PRIOR
#define TIME_RATIO_PRIOR 250
#endif
#define TIME_RATIO_MIN 150
#define TIME_RATIO_MAX 400

// 4 MB at depth 5, 64 MB at depth 6 and 256 MB at depth 7 and above
#ifndef HASH_TABLE_BITS
#define HASH_TABLE_BITS_FOR_DEPTH (3 * DEFAULT_SEARCH_DEPTH + 3)
#if HASH_TABLE_BITS_FOR_DEPTH < 18
#define HASH_TABLE_BITS 18
#elif HASH_TABLE_BITS_FOR_DEPTH > 23
#define HASH_TABLE_BITS 23
#else
#define HASH_TABLE_BITS HASH_TABLE_BITS_FOR_DEPTH
#endif
#endif
#define HASH_TABLE_SIZE (1 << HASH_TABLE_BITS)

#define DRAW_SCORE 0

#define TYPE_EXACT 1
#define TYPE_UPPER_BOUND 2
#define TYPE_LOWER_BOUND 3

#define WHITE_COLOR 1
#define BLACK_COLOR 2
#define NO_COLOR 3

#define NO_EN_PASSANT -2

#define PROMOTION_QUEEN 1
#define PROMOTION_KNIGHT 2
#define PROMOTION_BISHOP 3
#define PROMOTION_ROOK 4

#define CASTLING_WHITE_RIGHT 0
#define CASTLING_WHITE_LEFT 1
#define CASTLING_BLACK_RIGHT 2
#define CASTLING_BLACK_LEFT 3

#define MAX_SUPPORTED_OB_RULES 64

void fen_to_board(board_t * board, board_ext_t * board_ext, const char * fen_str);
void board_to_fen(char * fen_str, const board_t * board, const board_ext_t * board_ext);

#endif
