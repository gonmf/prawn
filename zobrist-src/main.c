/*
Generate a Zobrist key table file for prawn.

The file is a flat array of int64_t keys in native byte order, named
zobrist_N.bin and read whole by populate_zobrist_masks in prawn.c. That function
splits it into 64*12 piece-square keys, MAX_TOTAL_SEARCH_DEPTH+1 depth keys, 8
en passant file keys and 4 castling keys, in that order -- so the only thing
that varies with the search depth is how many keys the file has to hold, and
this tool only has to produce that many good keys.

Keys are drawn from /dev/urandom and constrained to have exactly 32 set bits
each and to be at least MIN_HAMMING_DISTANCE apart, which also makes them
distinct. Whole tables are then drawn over and over, keeping the one whose
per-bit column frequencies have the lowest variance, until interrupted with
ENTER or until the time budget runs out.
*/

#include "../common.h"

#include <sys/select.h>
#include <sys/types.h>

#define BITS_PER_KEY 64
#define SET_BITS_PER_KEY 32

// Minimum number of differing bits between any two keys in the table. Two
// random keys of weight 32 are 32 bits apart on average, so this only rejects
// far outliers -- among them duplicates, which are 0 bits apart.
#define MIN_HAMMING_DISTANCE 16

#define DEFAULT_SEARCH_DEPTH MAX_SEARCH_DEPTH
#define DEFAULT_SECONDS 10

static FILE * urandom_file = NULL;

static uint64_t random_u64() {
    static uint64_t pool[512];
    static int pool_left = 0;

    if (pool_left == 0) {
        pool_left = fread(pool, sizeof(uint64_t), 512, urandom_file);
        if (pool_left != 512) {
            fprintf(stderr, "Error: short read from /dev/urandom\n");
            exit(EXIT_FAILURE);
        }
    }

    return pool[--pool_left];
}

/*
Draw a uniformly random 64 bit value of exactly SET_BITS_PER_KEY set bits. Just
under one in ten random values qualifies, so rejection sampling is cheap and
keeps every qualifying value equally likely.
*/
static uint64_t random_key() {
    while (1) {
        uint64_t key = random_u64();

        if (__builtin_popcountll(key) == SET_BITS_PER_KEY) {
            return key;
        }
    }
}

static void fill_table(uint64_t * table, int entries) {
    for (int i = 0; i < entries; ++i) {
        while (1) {
            uint64_t key = random_key();

            int accepted = 1;
            for (int j = 0; j < i; ++j) {
                if (__builtin_popcountll(key ^ table[j]) < MIN_HAMMING_DISTANCE) {
                    accepted = 0;
                    break;
                }
            }

            if (accepted) {
                table[i] = key;
                break;
            }
        }
    }
}

/*
Variance of the number of times each of the 64 bits is set across the table. A
perfectly balanced table has every bit set in exactly half of the keys; the
lower this is, the less any single bit of the hash favours one value.
*/
static double column_variance(const uint64_t * table, int entries) {
    int counts[BITS_PER_KEY];
    memset(counts, 0, sizeof(counts));

    for (int i = 0; i < entries; ++i) {
        for (int b = 0; b < BITS_PER_KEY; ++b) {
            counts[b] += (table[i] >> b) & 1;
        }
    }

    double average = ((double)entries * SET_BITS_PER_KEY) / BITS_PER_KEY;
    double variance = 0.0;

    for (int b = 0; b < BITS_PER_KEY; ++b) {
        double diff = ((double)counts[b]) - average;
        variance += diff * diff;
    }

    return variance / BITS_PER_KEY;
}

static int min_hamming_distance(const uint64_t * table, int entries) {
    int min = BITS_PER_KEY;

    for (int i = 1; i < entries; ++i) {
        for (int j = 0; j < i; ++j) {
            int distance = __builtin_popcountll(table[i] ^ table[j]);
            if (distance < min) {
                min = distance;
            }
        }
    }

    return min;
}

/*
Whether anything is waiting on standard input, without blocking. Only meaningful
when standard input is a terminal; a redirected or closed one reads as ready
straight away and would stop the search before it started.
*/
static int interrupted() {
    if (!isatty(STDIN_FILENO)) {
        return 0;
    }

    fd_set readfs;
    FD_ZERO(&readfs);
    FD_SET(STDIN_FILENO, &readfs);

    struct timeval tm;
    tm.tv_sec = 0;
    tm.tv_usec = 0;

    return select(STDIN_FILENO + 1, &readfs, NULL, NULL, &tm) > 0;
}

static long elapsed_ms(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000L +
           (end.tv_usec - start.tv_usec) / 1000L;
}

/*
Number of keys prawn reads for a given maximum search depth, and the number it
names the file after. Kept in step with populate_zobrist_masks in prawn.c.
*/
static int entries_for_depth(int search_depth) {
    return 64 * 12 + (search_depth + QUIESCENCE_EXTRA_DEPTH) + 1 + 8 + 4;
}

static void print_usage(const char * program_name) {
    printf("Generate a Zobrist key table for prawn.\n\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  --depth=N    - Table for a maximum search depth of N (default %d)\n", DEFAULT_SEARCH_DEPTH);
    printf("  --entries=N  - Table of exactly N keys, instead of a depth\n");
    printf("  --seconds=N  - Search for a balanced table for N seconds (default %d)\n", DEFAULT_SECONDS);
    printf("  --force      - Overwrite the output file if it already exists\n");
    printf("  --help, -h   - Show this message\n\n");
    printf("Press ENTER to stop searching early and write the best table found.\n");
}

int main(int argc, char * argv[]) {
    int search_depth = DEFAULT_SEARCH_DEPTH;
    int entries = 0;
    int seconds = DEFAULT_SECONDS;
    int force = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strncmp(argv[i], "--depth=", strlen("--depth=")) == 0) {
            search_depth = atoi(argv[i] + strlen("--depth="));
        } else if (strncmp(argv[i], "--entries=", strlen("--entries=")) == 0) {
            entries = atoi(argv[i] + strlen("--entries="));
        } else if (strncmp(argv[i], "--seconds=", strlen("--seconds=")) == 0) {
            seconds = atoi(argv[i] + strlen("--seconds="));
        } else if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else {
            fprintf(stderr, "Unknown option %s\n\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (entries == 0) {
        if (search_depth < 1) {
            fprintf(stderr, "Error: search depth must be at least 1\n");
            return EXIT_FAILURE;
        }

        entries = entries_for_depth(search_depth);
    }

    if (entries < 1) {
        fprintf(stderr, "Error: table must have at least one key\n");
        return EXIT_FAILURE;
    }

    if (seconds < 1) {
        fprintf(stderr, "Error: time budget must be at least one second\n");
        return EXIT_FAILURE;
    }

    char filename[64];
    sprintf(filename, "zobrist_%d.bin", entries);

    if (!force) {
        FILE * existing = fopen(filename, "rb");
        if (existing != NULL) {
            fclose(existing);
            fprintf(stderr, "Error: %s already exists, pass --force to overwrite it\n", filename);
            return EXIT_FAILURE;
        }
    }

    urandom_file = fopen("/dev/urandom", "rb");
    if (urandom_file == NULL) {
        fprintf(stderr, "Error: failed to open /dev/urandom for reading\n");
        return EXIT_FAILURE;
    }

    uint64_t * table = malloc(sizeof(uint64_t) * entries);
    uint64_t * best_table = malloc(sizeof(uint64_t) * entries);
    if (table == NULL || best_table == NULL) {
        fprintf(stderr, "Error: failed to allocate two tables of %d keys\n", entries);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "Searching for a %d key table with an even bit distribution.\n", entries);
    fprintf(stderr, "Press ENTER to stop early; stopping on its own after %d seconds.\n\n", seconds);

    struct timeval start;
    struct timeval now;
    gettimeofday(&start, NULL);

    double best_variance = -1.0;
    long tables = 0;

    while (1) {
        fill_table(table, entries);
        ++tables;

        double variance = column_variance(table, entries);
        if (best_variance < 0.0 || variance < best_variance) {
            best_variance = variance;
            memcpy(best_table, table, sizeof(uint64_t) * entries);

            fprintf(stderr, "\rBest variance = %.5f after %ld tables ", best_variance, tables);
            fflush(stderr);
        }

        if (best_variance == 0.0 || interrupted()) {
            break;
        }

        gettimeofday(&now, NULL);
        if (elapsed_ms(start, now) >= seconds * 1000L) {
            break;
        }
    }

    fclose(urandom_file);
    free(table);

    fprintf(stderr, "\n\nSearch stopped after %ld tables.\n", tables);
    fprintf(stderr, "Bit distribution variance: %.5f\n", best_variance);
    fprintf(stderr, "Closest pair of keys: %d bits apart\n", min_hamming_distance(best_table, entries));

    FILE * out = fopen(filename, "wb");
    if (out == NULL) {
        fprintf(stderr, "Error: failed to open file %s for writing\n", filename);
        return EXIT_FAILURE;
    }

    if (fwrite(best_table, sizeof(uint64_t), entries, out) != (size_t)entries) {
        fprintf(stderr, "Error: unexpected number of keys written to %s\n", filename);
        fclose(out);
        return EXIT_FAILURE;
    }

    fclose(out);
    free(best_table);

    fprintf(stderr, "Zobrist table written to %s\n", filename);
    return EXIT_SUCCESS;
}
