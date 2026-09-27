#ifndef __MOVE_GEN_H_
#define __MOVE_GEN_H_

#include "common.h"

int piece_value(char piece);
int square_attacked_by(const board_t * board, int sq, int by_color);
uint64_t compute_pins(const board_t * board, int king_p, int own_color, uint64_t * pin_ray);
uint64_t attackers_to_square(const board_t * board, int sq, uint64_t occupied);
int static_exchange_eval(const board_t * board, const play_t * play, int mover_is_white);

void populate_pawn_capture_masks();
void populate_knight_moves_masks();
void populate_king_moves_masks();
int enumerate_all_piece_moves(play_t * valid_plays, const board_t * board, int captures_only);
int enumerate_legal_plays(play_t * valid_plays, const board_t * board, int captures_only, int * out_in_check);
int king_threatened(const board_t * board);

#endif
