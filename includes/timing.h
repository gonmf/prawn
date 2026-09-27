#ifndef __TIMING_H_
#define __TIMING_H_

long int elapsed_ms(struct timeval start, struct timeval end);
long int search_elapsed_ms();
int is_repetition(int64_t hash, int depth, int halfmoves);
int out_of_time();

#endif
