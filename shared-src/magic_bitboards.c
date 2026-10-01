#include "magic_bitboards.h"

uint64_t rook_masks[64];
uint64_t bishop_masks[64];
uint64_t rook_magics[64];
uint64_t bishop_magics[64];
int rook_shifts[64];
int bishop_shifts[64];

static uint64_t rook_table[64][4096];
static uint64_t bishop_table[64][512];

static const int rook_dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
static const int bishop_dirs[4][2] = { {1, 1}, {-1, 1}, {1, -1}, {-1, -1} };

// Squares that can block the piece, which is every square on its rays but the last of each: a man
// standing on the edge changes nothing, there being no square behind it to be kept from.
static uint64_t relevant_mask(int sq, const int dirs[4][2]) {
    uint64_t mask = 0ULL;
    int from_x = sq % 8;
    int from_y = sq / 8;

    for (int d = 0; d < 4; ++d) {
        int x = from_x + dirs[d][0];
        int y = from_y + dirs[d][1];

        // Stops one short of the board edge along each ray, and only along that ray: what rank a
        // square sits on says nothing about whether it can block a rook travelling sideways.
        while (x >= 0 && x < 8 && y >= 0 && y < 8) {
            int next_x = x + dirs[d][0];
            int next_y = y + dirs[d][1];

            if (next_x < 0 || next_x > 7 || next_y < 0 || next_y > 7) {
                break;
            }

            mask |= 1ULL << (y * 8 + x);
            x = next_x;
            y = next_y;
        }
    }

    return mask;
}

static uint64_t ray_attacks(uint64_t occupied, int sq, const int dirs[4][2]) {
    uint64_t attacks = 0ULL;
    int from_x = sq % 8;
    int from_y = sq / 8;

    for (int d = 0; d < 4; ++d) {
        int x = from_x + dirs[d][0];
        int y = from_y + dirs[d][1];

        while (x >= 0 && x < 8 && y >= 0 && y < 8) {
            uint64_t to_mask = 1ULL << (y * 8 + x);
            attacks |= to_mask;

            if (occupied & to_mask) {
                break;
            }

            x += dirs[d][0];
            y += dirs[d][1];
        }
    }

    return attacks;
}

static uint64_t random_state = 1;

static uint64_t next_random() {
    // Marsaglia's xorshift triples (2003)
    random_state ^= random_state >> 12;
    random_state ^= random_state << 25;
    random_state ^= random_state >> 27;

    return random_state * 0x2545F4914F6CDD1DULL;
}

// Few set bits, which is what makes a product that stacks the relevant bits together rather than
// smearing them across the word.
static uint64_t sparse_random() {
    return next_random() & next_random() & next_random();
}

// Lays every blocker pattern out through the magic, failing if two that attack different squares
// land on the same slot. Used both to test a candidate and to build the table from a known good one.
static int fill_table(int sq, const int dirs[4][2], uint64_t mask, int shift, uint64_t magic,
        uint64_t * table, int table_size) {
    for (int i = 0; i < table_size; ++i) {
        table[i] = 0ULL;
    }

    int filled[4096] = {0};
    uint64_t subset = 0ULL;

    do {
        uint64_t attacks = ray_attacks(subset, sq, dirs);
        int index = (int)((subset * magic) >> shift);

        if (index >= table_size) {
            return 0;
        }

        if (!filled[index]) {
            filled[index] = 1;
            table[index] = attacks;
        } else if (table[index] != attacks) {
            return 0;
        }

        subset = (subset - mask) & mask;
    } while (subset);

    return 1;
}

// A constant whose product with every blocker pattern, shifted down, lands each pattern on its own
// slot. Two patterns may share one as long as they attack the same squares, which is common enough
// that a constant can be found at all.
static uint64_t find_magic(int sq, const int dirs[4][2], uint64_t mask, int shift,
        uint64_t * table, int table_size) {
    uint64_t patterns[4096];
    uint64_t attacks[4096];
    int seen[4096];
    int count = 0;

    uint64_t subset = 0ULL;
    do {
        patterns[count] = subset;
        attacks[count] = ray_attacks(subset, sq, dirs);
        count++;
        subset = (subset - mask) & mask;
    } while (subset);

    for (int i = 0; i < table_size; ++i) {
        seen[i] = -1;
    }

    for (int attempt = 1; ; ++attempt) {
        uint64_t magic = sparse_random();

        if (__builtin_popcountll((mask * magic) & 0xFF00000000000000ULL) < 6) {
            continue;
        }

        int failed = 0;
        for (int i = 0; i < count; ++i) {
            int index = (int)((patterns[i] * magic) >> shift);

            if (seen[index] != attempt) {
                seen[index] = attempt;
                table[index] = attacks[i];
            } else if (table[index] != attacks[i]) {
                failed = 1;
                break;
            }
        }

        if (!failed) {
            return magic;
        }
    }
}

static void populate_masks_and_shifts() {
    for (int sq = 0; sq < 64; ++sq) {
        rook_masks[sq] = relevant_mask(sq, rook_dirs);
        bishop_masks[sq] = relevant_mask(sq, bishop_dirs);
        rook_shifts[sq] = 64 - __builtin_popcountll(rook_masks[sq]);
        bishop_shifts[sq] = 64 - __builtin_popcountll(bishop_masks[sq]);
    }
}

// Searching for the constants takes long enough to be worth doing once, offline. This is what the
// magic-src tool calls; the engine itself only ever reads the result back.
void search_magics(uint64_t * rook_out, uint64_t * bishop_out) {
    populate_masks_and_shifts();

    for (int sq = 0; sq < 64; ++sq) {
        rook_out[sq] = find_magic(sq, rook_dirs, rook_masks[sq], rook_shifts[sq],
            rook_table[sq], 1 << __builtin_popcountll(rook_masks[sq]));
        bishop_out[sq] = find_magic(sq, bishop_dirs, bishop_masks[sq], bishop_shifts[sq],
            bishop_table[sq], 1 << __builtin_popcountll(bishop_masks[sq]));
    }
}

static int read_magic_file(const char * name, uint64_t * dest) {
    FILE * fp = open_data_file(name, "rb");
    if (fp == NULL) {
        return 0;
    }

    int read = (int)fread(dest, sizeof(uint64_t), 64, fp);
    fclose(fp);

    return read == 64;
}

int populate_magic_bitboards() {
    populate_masks_and_shifts();

    if (!read_magic_file("magic_rook.bin", rook_magics) || !read_magic_file("magic_bishop.bin", bishop_magics)) {
        return 0;
    }

    for (int sq = 0; sq < 64; ++sq) {
        if (!fill_table(sq, rook_dirs, rook_masks[sq], rook_shifts[sq], rook_magics[sq],
                rook_table[sq], 1 << __builtin_popcountll(rook_masks[sq]))) {
            return 0;
        }
        if (!fill_table(sq, bishop_dirs, bishop_masks[sq], bishop_shifts[sq], bishop_magics[sq],
                bishop_table[sq], 1 << __builtin_popcountll(bishop_masks[sq]))) {
            return 0;
        }
    }

    return 1;
}

uint64_t magic_rook_attacks(int sq, uint64_t occupied) {
    uint64_t blockers = occupied & rook_masks[sq];

    return rook_table[sq][(blockers * rook_magics[sq]) >> rook_shifts[sq]];
}

uint64_t magic_bishop_attacks(int sq, uint64_t occupied) {
    uint64_t blockers = occupied & bishop_masks[sq];

    return bishop_table[sq][(blockers * bishop_magics[sq]) >> bishop_shifts[sq]];
}
