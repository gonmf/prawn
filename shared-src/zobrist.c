#include "zobrist.h"

void populate_zobrist_masks() {
    int nItems = 64 * 12 + 1 + 8 + 4;
    sprintf(buffer, "zobrist_%d.bin", nItems);
    FILE * s = open_data_file(buffer, "rb");
    if (s == NULL) {
        fprintf(stderr, "Zobrist file with %d entries not found.\n", nItems);
        exit(EXIT_FAILURE);
    }

    int64_t * zobrist_file = malloc(sizeof(int64_t) * nItems);
    if (zobrist_file == NULL) {
        fprintf(stderr, "Could not allocate %d Zobrist keys.\n", nItems);
        exit(EXIT_FAILURE);
    }

    if (fread(zobrist_file, sizeof(int64_t), nItems, s) != (size_t)nItems) {
        fprintf(stderr, "Zobrist file %s is too short, expected %d entries.\n", buffer, nItems);
        exit(EXIT_FAILURE);
    }
    fclose(s);

    int item = 0;

    for (int p = 0; p < 64; ++p) {
        for (int t = 0; t < 12; ++t) {
            zobrist_map[p][t] = zobrist_file[item++];
        }
    }

    zobrist_side_to_move = zobrist_file[item++];

    for (int t = 0; t < 8; ++t) {
        zobrist_en_passant[t] = zobrist_file[item++];
    }

    for (int t = 0; t < 4; ++t) {
        zobrist_castling[t] = zobrist_file[item++];
    }

    free(zobrist_file);
}

int64_t update_hash_with_piece(int64_t hash, int pos, char piece) {
    switch (piece) {
        case 'P': return hash ^ (zobrist_map[pos][0]);
        case 'p': return hash ^ (zobrist_map[pos][1]);
        case 'R': return hash ^ (zobrist_map[pos][2]);
        case 'r': return hash ^ (zobrist_map[pos][3]);
        case 'N': return hash ^ (zobrist_map[pos][4]);
        case 'n': return hash ^ (zobrist_map[pos][5]);
        case 'B': return hash ^ (zobrist_map[pos][6]);
        case 'b': return hash ^ (zobrist_map[pos][7]);
        case 'Q': return hash ^ (zobrist_map[pos][8]);
        case 'q': return hash ^ (zobrist_map[pos][9]);
        case 'K': return hash ^ (zobrist_map[pos][10]);
        default:  return hash ^ (zobrist_map[pos][11]);
    }
}
