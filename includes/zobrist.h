#ifndef __ZOBRIST_H_
#define __ZOBRIST_H_

#include "common.h"

void populate_zobrist_masks();
int64_t update_hash_with_piece(int64_t hash, int pos, char piece);

#endif
