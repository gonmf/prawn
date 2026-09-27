#ifndef __TRANSPOSITIONS_H_
#define __TRANSPOSITIONS_H_

#include "common.h"

extern uint8_t hash_table_age;
int pack_score(int score, int type);
int unpack_score(int score_w_type);
int16_t pack_play(const play_t * play);
int score_to_hash(int score, int depth);
int score_from_hash(int score, int depth);
int hash_table_bucket(int64_t hash);
void hash_table_reset();
hash_table_entry_t * hash_table_find(int64_t hash);
void hash_table_insert(int64_t hash, int score_w_type, int draft, int16_t best_play);

#endif
