#ifndef __COMMON_H_
#define __COMMON_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char b[64];
    char en_passant_x;
    char white_left_castling;
    char white_right_castling;
    char black_left_castling;
    char black_right_castling;
    char halfmoves;
    char fullmoves;
} board_t;

typedef struct {
    char from_x;
    char from_y;
    char to_x;
    char to_y;
    char promotion_option;
} play_t;

#define WHITE_COLOR 1
#define BLACK_COLOR 2
#define NO_COLOR 3

#define NO_EN_PASSANT -2

#define PROMOTION_QUEEN 1
#define PROMOTION_KNIGHT 2
#define PROMOTION_BISHOP 3
#define PROMOTION_ROOK 4

void fen_to_board(board_t * board, char * color, const char * fen_str);
void board_to_fen(char * fen_str, const board_t * board, char color);

#endif
