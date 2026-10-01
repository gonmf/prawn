#ifndef __MAGIC_BITBOARDS_H_
#define __MAGIC_BITBOARDS_H_

#include "common.h"

int populate_magic_bitboards();
void search_magics(uint64_t * rook_out, uint64_t * bishop_out);
uint64_t magic_rook_attacks(int sq, uint64_t occupied);
uint64_t magic_bishop_attacks(int sq, uint64_t occupied);

#endif
