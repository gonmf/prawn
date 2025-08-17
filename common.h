#ifndef __COMMON_H_
#define __COMMON_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h>

#define PROGRAM_VERSION "1.0"

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
    char promotion_option;
} play_t;

typedef struct {
    int64_t hash;
    int score_w_type;
} hash_table_entry_t;

#define MAX_SEARCH_DEPTH 5
#define HASH_TABLE_SIZE 8388608

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

void fen_to_board(board_t * board, unsigned int * fullmoves, const char * fen_str);
void board_to_fen(char * fen_str, const board_t * board, unsigned int fullmoves);
void board_to_short_string(char * str, const board_t * board);

#endif
