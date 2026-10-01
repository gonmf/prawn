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

// A passer with a pieqce in front of it is worth a fraction of a free one, and one the enemy king
// cannot catch is worth close to the queen it becomes.
#define PASSED_BLOCKED_DIV 2
#define PASSED_PROTECTED 15
#define PASSED_PHALANX 20
#define PASSED_UNSTOPPABLE 600

// An enemy king standing on the path holds the passer back whatever rank it has reached.
#define PASSED_KING_HELD_DIV 3
#define PASSED_KING_DISTANCE 10

// Pawns that cannot be defended by another pawn, or that stand in each other's way.
#define DOUBLED_PAWN 12
#define ISOLATED_PAWN 15
#define BACKWARD_PAWN 10

// Pawn structure repeats across huge numbers of nodes, so the part of the evaluation that depends
// on nothing but the two pawn bitboards is worth keeping.
#ifndef PAWN_HASH_BITS
#define PAWN_HASH_BITS 16
#endif
#define PAWN_HASH_SIZE (1 << PAWN_HASH_BITS)

// Squares a piece bears on that a pawn does not already deny it, counted against what the piece
// could expect to have. Rooks gain most from an open board, so their count is worth more late.
#define MOBILITY_KNIGHT_BASE 4
#define MOBILITY_BISHOP_BASE 6
#define MOBILITY_ROOK_BASE 7
#define MOBILITY_QUEEN_BASE 13
#define MOBILITY_KNIGHT_MG 4
#define MOBILITY_KNIGHT_EG 4
#define MOBILITY_BISHOP_MG 3
#define MOBILITY_BISHOP_EG 3
#define MOBILITY_ROOK_MG 2
#define MOBILITY_ROOK_EG 4
#define MOBILITY_QUEEN_MG 1
#define MOBILITY_QUEEN_EG 2

// Attackers on the squares around a king, weighted and then squared: two pieces bearing on a king
// are worth far more than twice one. Only while there are pieces left to do it with.
#define KING_ATTACK_KNIGHT 20
#define KING_ATTACK_BISHOP 20
#define KING_ATTACK_ROOK 40
#define KING_ATTACK_QUEEN 80
#define KING_DANGER_DIV 256
#define KING_DANGER_MAX 500

// What stands in front of a king of its own.
#define KING_OPEN_FILE 25
#define KING_SHIELD_ADVANCED 9

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

// Must fit a whole game position command in UCI
#define INPUT_BUFFER_SIZE 16384

#define NO_SCORE -536870912
#define CHECK_MATE -536870911
#define DRAW 536870911

// A checkmate is worth MATE_SCORE less 128 for every play it takes to reach, so that the search
// prefers the shortest one. No material evaluation comes close.
#define MATE_SCORE 20000000
#define MATE_THRESHOLD (MATE_SCORE - 128 * (MAX_TOTAL_SEARCH_DEPTH + 1))

#define PAWN_VALUE 100
#define KNIGHT_VALUE 320
#define BISHOP_VALUE 330
#define ROOK_VALUE 500
#define QUEEN_VALUE 900
#define KING_VALUE 20000

// How far short of alpha a capture may leave the node before it is not worth searching
#define DELTA_MARGIN 200

// Ordering bonuses for quiet plays. Both sit far above any history count, which is capped.
#define KILLER_FIRST (1 << 28)
#define KILLER_SECOND (1 << 27)
#define HISTORY_MAX (1 << 20)

#define MIDGAME_MATERIAL 6400
#define ENDGAME_MATERIAL 1300
#define MATE_DRIVE_EDGE 10
#define MATE_DRIVE_CLOSE 4

#define MAX(A,B) ((A) > (B) ? (A) : (B))
#define MIN(A,B) ((A) < (B) ? (A) : (B))

extern char buffer[INPUT_BUFFER_SIZE];

extern board_t board;
extern board_ext_t board_ext;

extern uint64_t white_pawn_capture_masks[64];
extern uint64_t black_pawn_capture_masks[64];
extern uint64_t white_en_passant_capture_masks[8];
extern uint64_t black_en_passant_capture_masks[8];
extern uint64_t knight_moves_masks[64];
extern uint64_t king_moves_masks[64];

extern int64_t zobrist_map[64][12];
extern int64_t zobrist_side_to_move; // hashes when black
extern int64_t zobrist_en_passant[8];
extern int64_t zobrist_castling[4];

extern hash_table_entry_t * hash_table;

extern int64_t search_history[MAX_GAME_PLAYS + MAX_TOTAL_SEARCH_DEPTH + 4];
extern int search_history_count;

extern struct timeval search_start;
extern long int search_soft_ms;
extern long int search_budget_ms;
extern int search_depth_limit;
extern int search_aborted;
extern uint64_t search_nodes;
extern int search_abortable;

extern unsigned int opening_book_size;
extern uint64_t opening_book[MAX_SUPPORTED_OB_RULES];
extern char ob_play_colors[MAX_SUPPORTED_OB_RULES];
extern play_short_t ob_plays[MAX_SUPPORTED_OB_RULES][4];

extern int opening_book_enabled;
extern int extend_uci;
extern int arbitrate_draws;
extern int uci_game_in_error_state;
extern int convert_at_ob_depth;
extern char program_dir[1024];

void fen_to_board(board_t * board, board_ext_t * board_ext, const char * fen_str);
void board_to_fen(char * fen_str, const board_t * board, const board_ext_t * board_ext);

#include "timing.h"
#include "board.h"
#include "move_gen.h"
#include "zobrist.h"
#include "filesystem.h"
#include "magic_bitboards.h"
#include "transpositions.h"
#include "opening_book.h"
#include "evaluation.h"
#include "search.h"
#include "uci.h"
#include "text_ui.h"

#endif
