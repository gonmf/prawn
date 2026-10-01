/*
Searches for the magic constants prawn uses to index its sliding piece attack tables, and writes
them to magic_rook.bin and magic_bishop.bin.

The search is thousands of random trials per square and takes a fraction of a second, which is a
fraction of a second the engine should not spend on every start. The files are in the repository,
so this only matters if you want to make your own.

Each file is a flat array of 64 uint64_t in native byte order, one per square, in the same layout
as zobrist_N.bin. The masks and shifts are not stored: they follow from the board geometry and the
engine derives them the same way this tool does.
*/

#include "common.h"

static int write_magics(const char * name, const uint64_t * magics) {
    FILE * fp = fopen(name, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for writing.\n", name);
        return 0;
    }

    size_t written = fwrite(magics, sizeof(uint64_t), 64, fp);
    fclose(fp);

    if (written != 64) {
        fprintf(stderr, "Could only write %zu of 64 keys to %s.\n", written, name);
        return 0;
    }

    printf("Wrote %s\n", name);
    return 1;
}

int main() {
    uint64_t rook_magics_found[64];
    uint64_t bishop_magics_found[64];

    struct timeval start;
    struct timeval end;

    gettimeofday(&start, NULL);
    search_magics(rook_magics_found, bishop_magics_found);
    gettimeofday(&end, NULL);

    printf("Searched in %ld ms.\n", elapsed_ms(start, end));

    if (!write_magics("magic_rook.bin", rook_magics_found)
            || !write_magics("magic_bishop.bin", bishop_magics_found)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
