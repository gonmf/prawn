#ifndef __BOARD_H_
#define __BOARD_H_

#include "common.h"

char identify_piece(const board_t * board, int p);
char identify_piece_of(const board_t * board, int p, int color);

void refresh_masks(board_t * board);
void just_play_white_simple(board_t * board, const play_t * play);
void just_play_black_simple(board_t * board, const play_t * play);
void just_play_white_pawn(board_t * board, const play_t * play, int64_t * out_hash);
void just_play_white_complex(board_t * board, const play_t * play, int64_t * out_hash);
void just_play_black_pawn(board_t * board, const play_t * play, int64_t * out_hash);
void just_play_black_complex(board_t * board, const play_t * play, int64_t * out_hash);
void actual_play(board_t * board, board_ext_t * board_ext, const play_t * play);
int64_t hash_from_board(const board_t * board);
void reset_board();
void undo_last_plays();
int insufficient_material(const board_t * board);
int is_game_drawn();

#endif
