#ifndef __FEN_H_
#define __FEN_H_

#include "common.h"

void fen_to_board(board_t * board, board_ext_t * board_ext, const char * fen_str);
void board_to_fen(char * fen_str, const board_t * board, const board_ext_t * board_ext);

#endif
