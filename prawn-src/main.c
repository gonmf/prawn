#include "common.h"

static void show_help() {
    printf("Prawn %s\n\nOptions:\n", PROGRAM_VERSION);
    printf("  --from-fen FEN    - Start from FEN position\n");
    printf("  --no-book         - Do not use the opening book\n");
    printf("  --convert-ob-at=N - Convert opening book to FEN list for positions at depth N\n");
    printf("  --uci             - Start on UCI interface mode (default)\n");
    printf("  --text            - Play via the text interface\n");
    printf("  --self            - Have the program play against itself\n");
    printf("  --extend-uci      - In UCI mode reply loss/draw to position commands\n");
    printf("  --help, -h        - Show this message\n\n");
}

static void init_randomness() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned int seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec ^ getpid());
    srand(seed);
}

int main(int argc, char * argv[]) {
    init_randomness();
    set_program_dir(argv[0]);
    populate_pawn_capture_masks();
    populate_passed_pawn_masks();
    populate_knight_moves_masks();
    populate_king_moves_masks();
    populate_zobrist_masks();
    if (posix_memalign((void **)&hash_table, 64, (size_t)HASH_TABLE_SIZE * sizeof(hash_table_entry_t)) != 0) {
        hash_table = NULL;
    }
    if (hash_table == NULL) {
        fprintf(stderr, "Could not allocate the %zu MB transposition table.\n", ((size_t)HASH_TABLE_SIZE * sizeof(hash_table_entry_t)) / (1024 * 1024));
        return EXIT_FAILURE;
    }

    hash_table_reset();

    int from_fen_idx = -1;
    char mode = 'u';

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--from-fen") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                from_fen_idx = i + 1;
                i++;
            } else {
                printf("Incorrect argument after --from-fen\n");
            }
            continue;
        }
        if (strcmp(argv[i], "--no-book") == 0) {
            opening_book_enabled = 0;
            continue;
        }
        if (strncmp(argv[i], "--convert-ob-at=", strlen("--convert-ob-at=")) == 0) {
            convert_at_ob_depth = atoi(argv[i] + strlen("--convert-ob-at="));
            continue;
        }
        if (strcmp(argv[i], "--uci") == 0) {
            mode = 'u';
            continue;
        }
        if (strcmp(argv[i], "--text") == 0) {
            mode = 't';
            continue;
        }
        if (strcmp(argv[i], "--self") == 0) {
            mode = 's';
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            show_help();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--extend-uci") == 0) {
            extend_uci = 1;
            continue;
        }
        printf("Unrecognized argument \"%s\"\n", argv[i]);
        return EXIT_FAILURE;
    }

    if (opening_book_enabled) {
        init_opening_book();
    } else if (convert_at_ob_depth != -1) {
        fprintf(stderr, "Error: --convert-ob-at argument disallows --no-book\n");
        return EXIT_FAILURE;
    }

    reset_board();

    if (from_fen_idx != -1) {
        fen_to_board(&board, &board_ext, argv[from_fen_idx]);
    }

    if (mode == 'u') {
        FILE * fd = init_log_file(argv[0]);
        uci_mode(fd);
    } else if (mode == 's') {
        self_play();
    } else {
        text_mode();
    }
    return EXIT_SUCCESS;
}
