#ifndef __SEARCH_H_
#define __SEARCH_H_

#include "common.h"

int negamax_capture_only(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash);
void record_quiet_cutoff(int color_index, int depth, const play_t * play, int draft);
int negamax( const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash, int can_null_prune );
long int predicted_iteration_ms(const long int * iter_ms, int depth);
long int time_allowance(int changed, int score_drop);
int ai_play(play_t * play);

#endif
