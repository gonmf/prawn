#ifndef __TEXT_UI_H_
#define __TEXT_UI_H_

#include "common.h"

void fprint_board(FILE * fd, const board_t * board, const board_ext_t * board_ext);
void print_board(const board_t * board, const board_ext_t * board_ext);
void read_input_line();
char input_promotion_piece();
char * read_play(play_t * play, char * str);
int input_play(play_t * play, const play_t * valid_plays, int valid_plays_i);
void text_mode_play(int player_one_is_human, int player_two_is_human);
void text_mode();
void self_play();

#endif
