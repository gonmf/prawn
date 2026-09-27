#include "transpositions.h"

uint8_t hash_table_age = 0;

// A hash table value is a score in the upper 30 bits and a TYPE_* tag in the lower 2. Scores must
// therefore fit in 30 bits signed; the largest magnitude ever stored is a mate score, ~20000000.
int pack_score(int score, int type) {
    return (int)(((uint32_t)score << 2) | (uint32_t)type);
}

int unpack_score(int score_w_type) {
    return score_w_type >> 2;
}

// A play packed into 16 bits: origin square and destination square in 6 bits each, promotion in 4.
//  No legal play stays on its own square, so zero means "no play".
int16_t pack_play(const play_t * play) {
    int from_p = play->from_y * 8 + play->from_x;
    int to_p = play->to_y * 8 + play->to_x;

    return (int16_t)(from_p | (to_p << 6) | (((int)play->promotion_option) << 12));
}

// Mate scores include play counts from the root
int score_to_hash(int score, int depth) {
    if (score > MATE_THRESHOLD) {
        return score + depth * 128;
    }
    if (score < -MATE_THRESHOLD) {
        return score - depth * 128;
    }
    return score;
}

int score_from_hash(int score, int depth) {
    if (score > MATE_THRESHOLD) {
        return score - depth * 128;
    }
    if (score < -MATE_THRESHOLD) {
        return score + depth * 128;
    }
    return score;
}

// cache aligned
int hash_table_bucket(int64_t hash) {
    return (int)(hash & (HASH_TABLE_SIZE - 1)) & ~(HASH_TABLE_BUCKET - 1);
}

void hash_table_reset() {
    if (hash_table != NULL) {
        bzero(hash_table, HASH_TABLE_SIZE * sizeof(hash_table_entry_t));
    }
    hash_table_age = 0;
}

// An entry is filled in if the 2 lowest bits of its score_w_type hold a TYPE_*, i.e. are not zero.
// A hit is marked as belonging to the current search, so that a position still being visited is not
// evicted by the positions around it.
hash_table_entry_t * hash_table_find(int64_t hash) {
    hash_table_entry_t * bucket = &hash_table[hash_table_bucket(hash)];

    for (int i = 0; i < HASH_TABLE_BUCKET; ++i) {
        hash_table_entry_t * entry = &bucket[i];

        if ((entry->score_w_type & 3) != 0 && entry->hash == hash) {
            entry->age = hash_table_age;
            return entry;
        }
    }
    return 0;
}

void hash_table_insert(int64_t hash, int score_w_type, int draft, int16_t best_play) {
    hash_table_entry_t * bucket = &hash_table[hash_table_bucket(hash)];

    hash_table_entry_t * victim = 0;
    int victim_value = 0;

    for (int i = 0; i < HASH_TABLE_BUCKET; ++i) {
        hash_table_entry_t * entry = &bucket[i];

        if ((entry->score_w_type & 3) == 0) {
            victim = entry;
            break;
        }

        if (entry->hash == hash) {
            // The same position, searched again. A result from this search that did not go as deep
            // as what is already there is not an improvement, but its play is still worth keeping
            // if the entry has none.
            if (draft < entry->draft && entry->age == hash_table_age) {
                if (best_play != 0) {
                    entry->best_play = best_play;
                }
                entry->age = hash_table_age;
                return;
            }
            victim = entry;
            break;
        }

        int value = entry->draft - (entry->age == hash_table_age ? 0 : 64);
        if (victim == 0 || value < victim_value) {
            victim = entry;
            victim_value = value;
        }
    }

    // A result with no play of its own -- a static evaluation, a finished game -- should not cost
    // this position the play it already had.
    if (best_play == 0 && victim->hash == hash && (victim->score_w_type & 3) != 0) {
        best_play = victim->best_play;
    }

    victim->hash = hash;
    victim->score_w_type = score_w_type;
    victim->best_play = best_play;
    victim->draft = (int8_t)draft;
    victim->age = hash_table_age;
}
