#ifndef __COMMON_H_
#define __COMMON_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PROGRAM_VERSION "1.0"

typedef struct {
    char b[64];
    char en_passant_x;
    char white_left_castling;
    char white_right_castling;
    char black_left_castling;
    char black_right_castling;
    unsigned char halfmoves;
    unsigned char fullmoves;
    char color;
} board_t;

typedef struct {
    char from_x;
    char from_y;
    char to_x;
    char to_y;
    char promotion_option;
} play_t;

#define TYPE_EXACT 1
#define TYPE_UPPER_BOUND 2
#define TYPE_LOWER_BOUND 3

typedef struct {
    int score;
    char type;
} saved_score_t;

#define WHITE_COLOR 1
#define BLACK_COLOR 2
#define NO_COLOR 3

#define NO_EN_PASSANT -2

#define PROMOTION_QUEEN 1
#define PROMOTION_KNIGHT 2
#define PROMOTION_BISHOP 3
#define PROMOTION_ROOK 4

#define MAX_DEPTH 5

void fen_to_board(board_t * board, const char * fen_str);
void board_to_fen(char * fen_str, const board_t * board);
void board_to_short_string(char * str, const board_t * board);

#endif
