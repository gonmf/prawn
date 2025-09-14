// TODO:
// move quiescence

#include "common.h"

static char buffer[1024];

static char past_positions[256][56];
static unsigned char past_plays_count;
static board_t board;
static unsigned int fullmoves;
static char last_play_x = -1;
static char last_play_y = -1;
static int opening_book_enabled = 1;

#define PIECE_SCORE_MULTIPLIER 256
#define NO_SCORE -536870912
#define CHECK_MATE -536870911
#define DRAW 536870911

#define MAX(A,B) ((A) > (B) ? (A) : (B))
#define MIN(A,B) ((A) < (B) ? (A) : (B))

static uint64_t white_pawn_capture_masks[64];
static uint64_t black_pawn_capture_masks[64];
static uint64_t white_en_passant_capture_masks[8];
static uint64_t black_en_passant_capture_masks[8];
static uint64_t knight_moves_masks[64];
static uint64_t king_moves_masks[64];

static int64_t zobrist_map[64][12];
static int64_t zobrist_depth[MAX_SEARCH_DEPTH + 1];
static int64_t zobrist_en_passant[8];
static int64_t zobrist_castling[4];

static hash_table_entry_t * hash_table;

static unsigned int opening_book_size;
static uint64_t opening_book[MAX_SUPPORTED_OB_RULES];
static play_short_t ob_plays[MAX_SUPPORTED_OB_RULES][4];

static void reset_board();
static void actual_play(const play_t * play);
static int64_t hash_from_board(const board_t * board, int depth);

static void init_opening_book() {
    opening_book_size = 0;
    srand(time(NULL));

    play_t play;

    play.promotion_option = 0;
    FILE * fp = fopen("openings.txt", "r");

    while (opening_book_size < MAX_SUPPORTED_OB_RULES) {
        char * r = fgets(buffer, 16 * 1024, fp);
        if (r == NULL) {
            break;
        }

        char * s = buffer;
        s[strlen(s) - 1] = 0;
        if (s[0] == 0 || s[0] == '#') {
            continue;
        }

        reset_board();

        int plays_found = -1;
        char * token;
        while ((token = strsep(&s, " ")) != NULL && plays_found < 4) {
            if (token[0] == '|') {
                plays_found = 0;
                continue;
            }

            if (plays_found == -1) {
                play.from_x = token[0] - 'a';
                play.from_y = '8' - token[1];
                play.to_x = token[2] - 'a';
                play.to_y = '8' - token[3];

                actual_play(&play);
                continue;
            }

            ob_plays[opening_book_size][plays_found].from_x = token[0] - 'a';
            ob_plays[opening_book_size][plays_found].from_y = '8' - token[1];
            ob_plays[opening_book_size][plays_found].to_x = token[2] - 'a';
            ob_plays[opening_book_size][plays_found].to_y = '8' - token[3];
            plays_found++;
        }

        if (plays_found != 4) {
            fprintf(stderr, "Error loading opening book\n");
            exit(EXIT_FAILURE);
        }

        opening_book[opening_book_size] = hash_from_board(&board, 0);
        opening_book_size++;
    }

    fclose(fp);
}

static int get_opening_book_play(play_t * play, uint64_t board_hash) {
    for (unsigned int i = 0; i < opening_book_size; ++i) {
        if (board_hash == opening_book[i]) {
            int play_picked = rand() % 4;

            play->from_x = ob_plays[i][play_picked].from_x;
            play->from_y = ob_plays[i][play_picked].from_y;
            play->to_x = ob_plays[i][play_picked].to_x;
            play->to_y = ob_plays[i][play_picked].to_y;
            play->promotion_option = 0;
            return 1;
        }
    }

    return 0;
}

static void populate_pawn_capture_masks() {
    int x2, y2;

    for (int p = 0; p < 64; ++p) {
        white_pawn_capture_masks[p] = 0ULL;
        black_pawn_capture_masks[p] = 0ULL;

        int x = p % 8;
        int y = p / 8;

        x2 = x - 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x - 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }
    }

    for (int x = 0; x < 8; ++x) {
        white_en_passant_capture_masks[x] = 0ULL;
        black_en_passant_capture_masks[x] = 0ULL;

        int y = 2;

        x2 = x - 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }

        y = 5;

        x2 = x - 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }
    }
}

static void populate_knight_moves_masks() {
    const int offsets[8][2] = {
        {-1, -2}, {+1, -2}, {-2, -1}, {+2, -1},
        {-2, +1}, {+2, +1}, {-1, +2}, {+1, +2}
    };

    for (int p = 0; p < 64; ++p) {
        int x = p % 8, y = p / 8;
        knight_moves_masks[p] = 0ULL;

        for (int i = 0; i < 8; i++) {
            int x2 = x + offsets[i][0];
            int y2 = y + offsets[i][1];
            if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
                knight_moves_masks[p] |= (1ULL << (y2 * 8 + x2));
            }
        }
    }
}

static void populate_king_moves_masks() {
    const int offsets[8][2] = {
        {-1, -1}, {0, -1}, {+1, -1},
        {-1,  0},          {+1,  0},
        {-1, +1}, {0, +1}, {+1, +1}
    };

    for (int p = 0; p < 64; ++p) {
        int x = p % 8, y = p / 8;
        king_moves_masks[p] = 0ULL;

        for (int i = 0; i < 8; i++) {
            int x2 = x + offsets[i][0];
            int y2 = y + offsets[i][1];
            if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
                king_moves_masks[p] |= (1ULL << (y2 * 8 + x2));
            }
        }
    }
}

// We tell if the entry is filled in by the 2 lowest bits of the score_w_type present, that must be zero
static int hash_table_find(int * score_w_type, int64_t hash) {
    int start_key = hash & (HASH_TABLE_SIZE - 1);
    int i = 0;

    while (i < 5) {
        int key = (start_key + i) & (HASH_TABLE_SIZE - 1);
        if (hash_table[key].hash == hash) {
            *score_w_type = hash_table[key].score_w_type;
            return 1;
        }

        ++i;
    }
    return 0;
}

static void hash_table_insert(int64_t hash, int score_w_type) {
    int start_key = hash & (HASH_TABLE_SIZE - 1);
    int i = 0;

    while (i < 5) {
        int key = (start_key + i) & (HASH_TABLE_SIZE - 1);
        if ((hash_table[key].score_w_type & 3) == 0) {
            hash_table[key].hash = hash;
            hash_table[key].score_w_type = score_w_type;
            return;
        }

        ++i;
    }
}

static void populate_zobrist_masks() {
    int nItems = 64 * 12 + MAX_SEARCH_DEPTH + 1 + 4;
    sprintf(buffer, "zobrist_%d.bin", (nItems & 1) ? nItems + 1 : nItems);
    FILE * s = fopen(buffer, "rb");
    if (s == NULL) {
        fprintf(stderr, "Zobrist file with %d entries (depth %d) not found.\n", nItems, MAX_SEARCH_DEPTH);
        exit(EXIT_FAILURE);
    }

    int64_t * zobrist_file = malloc(sizeof(int64_t) * nItems);
    fread(zobrist_file, sizeof(int64_t), nItems, s);
    fclose(s);

    int item = 0;

    for (int p = 0; p < 64; ++p) {
        for (int t = 0; t < 12; ++t) {
            zobrist_map[p][t] = zobrist_file[item++];
        }
    }

    for (int t = 0; t < MAX_SEARCH_DEPTH + 1; ++t) {
        zobrist_depth[t] = zobrist_file[item++];
    }

    for (int t = 0; t < 4; ++t) {
        zobrist_castling[t] = zobrist_file[item++];
    }

    free(zobrist_file);
}

static int64_t update_hash_with_piece_white(int64_t hash, int pos, char piece) {
    switch (piece) {
        case 'P': return hash ^ (zobrist_map[pos][0]);
        case 'R': return hash ^ (zobrist_map[pos][2]);
        case 'N': return hash ^ (zobrist_map[pos][4]);
        case 'B': return hash ^ (zobrist_map[pos][6]);
        case 'K': return hash ^ (zobrist_map[pos][10]);
        default:  return hash ^ (zobrist_map[pos][8]);
    }
}

static int64_t update_hash_with_piece_black(int64_t hash, int pos, char piece) {
    switch (piece) {
        case 'p': return hash ^ (zobrist_map[pos][1]);
        case 'r': return hash ^ (zobrist_map[pos][3]);
        case 'n': return hash ^ (zobrist_map[pos][5]);
        case 'b': return hash ^ (zobrist_map[pos][7]);
        case 'k': return hash ^ (zobrist_map[pos][11]);
        default:  return hash ^ (zobrist_map[pos][9]);
    }
}

static int64_t update_hash_with_piece(int64_t hash, int pos, char piece) {
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

static char identify_piece(const board_t * board, int p) {
    uint64_t mask = (1ULL << p);

    if (board->white_pawns & mask)   return 'P';
    if (board->black_pawns & mask)   return 'p';
    if (board->white_knights & mask) return 'N';
    if (board->black_knights & mask) return 'n';
    if (board->white_bishops & mask) return 'B';
    if (board->black_bishops & mask) return 'b';
    if (board->white_rooks & mask)   return 'R';
    if (board->black_rooks & mask)   return 'r';
    if (board->white_queens & mask)  return 'Q';
    if (board->black_queens & mask)  return 'q';
    if (board->white_kings & mask)   return 'K';
    if (board->black_kings & mask)   return 'k';
    return ' ';
}

static char identify_piece_white(const board_t * board, int p) {
    uint64_t mask = (1ULL << p);

    if (board->white_pawns & mask)   return 'P';
    if (board->white_knights & mask) return 'N';
    if (board->white_bishops & mask) return 'B';
    if (board->white_rooks & mask)   return 'R';
    if (board->white_queens & mask)  return 'Q';
    if (board->white_kings & mask)   return 'K';
    return ' ';
}

static char identify_piece_black(const board_t * board, int p) {
    uint64_t mask = (1ULL << p);

    if (board->black_pawns & mask)   return 'p';
    if (board->black_knights & mask) return 'n';
    if (board->black_bishops & mask) return 'b';
    if (board->black_rooks & mask)   return 'r';
    if (board->black_queens & mask)  return 'q';
    if (board->black_kings & mask)   return 'k';
    return ' ';
}

static int64_t hash_from_board(const board_t * board, int depth) {
    int64_t hash = 0xAAAAAAAAAAAAAAAA;

    for (int p = 0; p < 64; ++p) {
        char piece = identify_piece(board, p);
        if (piece != ' ') {
            hash = update_hash_with_piece(hash, p, piece);
        }
    }

    if (board->en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[(int)(board->en_passant_x)];
    }

    if (board->white_right_castling) {
        hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
    }
    if (board->white_left_castling) {
        hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
    }
    if (board->black_right_castling) {
        hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
    }
    if (board->black_left_castling) {
        hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
    }

    hash ^= zobrist_depth[depth];

    return hash;
}

static void print_board(const board_t * board) {
    board_to_fen(buffer, board, fullmoves);
    printf("%s\n", buffer);
    printf("╔═══╤═══╤═══╤═══╤═══╤═══╤═══╤═══╗┈╮\n");
    for (int y = 0; y < 8; y++) {
        printf("║ ");
        for (int x = 0; x < 8; x++) {
            if (x == last_play_x && y == last_play_y) {
                printf("\033[32m%c\e[0m", identify_piece(board, y * 8 + x));
            } else {
                printf("%c", identify_piece(board, y * 8 + x));
            }
            if (x < 7) {
                printf(" │ ");
            } else {
                printf(" ║ %c", 8 - y + '0');
            }
        }
        printf(("\n"));
        if (y < 7) {
            printf("╟───┼───┼───┼───┼───┼───┼───┼───╢ ┊\n");
        }
    }
    printf("╚═══╧═══╧═══╧═══╧═══╧═══╧═══╧═══╝ ┊\n");
    printf("╰┈a┈┈┈b┈┈┈c┈┈┈d┈┈┈e┈┈┈f┈┈┈g┈┈┈h┈┈┈╯\n\n");
}

static void just_play_white_simple(board_t * board, const play_t * play) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    char to_x = play->to_x;
    char to_y = play->to_y;
    char promotion_option = play->promotion_option;

    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;

    char from_piece = identify_piece_white(board, from_p);

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    board->black_pawns &= ~to_mask;
    board->black_knights &= ~to_mask;
    board->black_bishops &= ~to_mask;
    board->black_rooks &= ~to_mask;
    board->black_queens &= ~to_mask;
    board->black_kings &= ~to_mask;

    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'P':
            board->white_pawns ^= from_mask;
            if (promotion_option == 0) {
                board->white_pawns ^= to_mask;
            } else {
                switch (promotion_option) {
                    case PROMOTION_QUEEN: board->white_queens ^= to_mask;   break;
                    case PROMOTION_KNIGHT: board->white_knights ^= to_mask; break;
                    case PROMOTION_BISHOP: board->white_bishops ^= to_mask; break;
                    case PROMOTION_ROOK: board->white_rooks ^= to_mask;     break;
                }
            }
            break;
        case 'N':
            board->white_knights ^= from_mask;
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= from_mask;
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= from_mask;
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= from_mask;
            board->white_queens ^= to_mask;
            break;
        case 'K':
            board->white_kings ^= from_mask;
            board->white_kings ^= to_mask;
            break;
    }

    if (board->en_passant_x != NO_EN_PASSANT) {
        if (board->en_passant_x == to_x) {
            if (from_y == 3 && from_piece == 'P') {
                board->black_pawns ^= (1ULL << (from_y * 8 + to_x));
            }
        }
    }

    if (from_piece == 'K') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->white_rooks ^= (1ULL << (0 * 8 + 5));
                board->white_rooks ^= (1ULL << (0 * 8 + 7));
            } else if (to_x == 2) {
                board->white_rooks ^= (1ULL << (0 * 8 + 0));
                board->white_rooks ^= (1ULL << (0 * 8 + 3));
            }
        }
    }

    board->color = BLACK_COLOR;
}

static void just_play_black_simple(board_t * board, const play_t * play) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    char to_x = play->to_x;
    char to_y = play->to_y;
    char promotion_option = play->promotion_option;

    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;

    char from_piece = identify_piece_black(board, from_p);

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    board->white_pawns &= ~to_mask;
    board->white_knights &= ~to_mask;
    board->white_bishops &= ~to_mask;
    board->white_rooks &= ~to_mask;
    board->white_queens &= ~to_mask;
    board->white_kings &= ~to_mask;

    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'p':
            board->black_pawns ^= from_mask;
            if (promotion_option == 0) {
                board->black_pawns ^= to_mask;
            } else {
                switch (promotion_option) {
                    case PROMOTION_QUEEN: board->black_queens ^= to_mask;   break;
                    case PROMOTION_KNIGHT: board->black_knights ^= to_mask; break;
                    case PROMOTION_BISHOP: board->black_bishops ^= to_mask; break;
                    case PROMOTION_ROOK: board->black_rooks ^= to_mask;     break;
                }
            }
            break;
        case 'n':
            board->black_knights ^= from_mask;
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= from_mask;
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= from_mask;
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= from_mask;
            board->black_queens ^= to_mask;
            break;
        case 'k':
            board->black_kings ^= from_mask;
            board->black_kings ^= to_mask;
            break;
    }

    if (board->en_passant_x != NO_EN_PASSANT) {
        if (board->en_passant_x == to_x) {
            if (from_y == 4 && from_piece == 'p') {
                board->white_pawns ^= (1ULL << (from_y * 8 + to_x));
            }
        }
    }

    if (from_piece == 'k') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->black_rooks ^= (1ULL << (0 * 8 + 5));
                board->black_rooks ^= (1ULL << (0 * 8 + 7));
            } else if (to_x == 2) {
                board->black_rooks ^= (1ULL << (0 * 8 + 0));
                board->black_rooks ^= (1ULL << (0 * 8 + 3));
            }
        }
    }

    board->color = WHITE_COLOR;
}

static int just_play_white_pawn(board_t * board, const play_t * play, int score, int depth, int64_t * out_hash) {
    int from_x = play->from_x;
    int from_y = play->from_y;
    int to_x = play->to_x;
    int to_y = play->to_y;
    int en_passant_x = board->en_passant_x;
    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;
    char promotion_option = play->promotion_option;

    char from_piece = 'P';
    char to_piece = identify_piece_black(board, to_p);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_white(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_black(hash, to_p, to_piece);
    }

    hash ^= zobrist_depth[depth];
    hash ^= zobrist_depth[depth + 1];

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'p':
            board->black_pawns ^= to_mask;
            score += 1 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'n':
            board->black_knights ^= to_mask;
            score += 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'b':
            board->black_bishops ^= to_mask;
            score += 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'r':
            board->black_rooks ^= to_mask;
            score += 5 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'q':
            board->black_queens ^= to_mask;
            score += 9 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'k':
            board->black_kings ^= to_mask;
            break;
    }

    board->white_pawns ^= from_mask;

    if (en_passant_x != NO_EN_PASSANT) {
        if (en_passant_x == to_x && from_y == 3) {
            hash = update_hash_with_piece_black(hash, 3 * 8 + en_passant_x, 'p');
            board->black_pawns ^= to_mask;
            score += 1 * PIECE_SCORE_MULTIPLIER;
        }
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    if (to_y == 0) {
        if (to_x == 0) {
            if (board->black_left_castling) {
                board->black_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
            }
        } else if (to_x == 7) {
            if (board->black_right_castling) {
                board->black_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
            }
        }

        if (promotion_option == PROMOTION_QUEEN) {
            // 1 to 9
            score += 8 * PIECE_SCORE_MULTIPLIER;
            board->white_queens ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'Q');
        } else if (promotion_option == PROMOTION_KNIGHT) {
            // 1 to 3
            score += 2 * PIECE_SCORE_MULTIPLIER;
            board->white_knights ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'N');
        } else if (promotion_option == PROMOTION_BISHOP) {
            // 1 to 3
            score += 2 * PIECE_SCORE_MULTIPLIER;
            board->white_bishops ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'B');
        } else {
            // 1 to 5
            score += 4 * PIECE_SCORE_MULTIPLIER;
            board->white_rooks ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'R');
        }
    } else {
        board->white_pawns ^= to_mask;
        hash = update_hash_with_piece(hash, to_p, 'P');

        if (from_y == 6 && to_y == 4) {
            board->en_passant_x = from_x;
            hash ^= zobrist_en_passant[from_x];
        }
    }

    board->color = BLACK_COLOR;

    *out_hash = hash;
    return score;
}

static int just_play_white_complex(board_t * board, const play_t * play, int score, int depth, int64_t * out_hash) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    int from_p = from_y * 8 + from_x;

    char from_piece = identify_piece_white(board, from_p);
    if (from_piece == 'P') {
        return just_play_white_pawn(board, play, score, depth, out_hash);
    }

    char to_x = play->to_x;
    char to_y = play->to_y;
    int to_p = to_y * 8 + to_x;
    int en_passant_x = board->en_passant_x;

    char to_piece = identify_piece_black(board, to_p);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_white(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_black(hash, to_p, to_piece);
    }

    if (en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    hash ^= zobrist_depth[depth];

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'p':
            board->black_pawns ^= to_mask;
            score += 1 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'n':
            board->black_knights ^= to_mask;
            score += 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'b':
            board->black_bishops ^= to_mask;
            score += 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'r':
            board->black_rooks ^= to_mask;
            score += 5 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'q':
            board->black_queens ^= to_mask;
            score += 9 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'k':
            board->black_kings ^= to_mask;
            break;
    }
    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'N':
            board->white_knights ^= from_mask;
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= from_mask;
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= from_mask;
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= from_mask;
            board->white_queens ^= to_mask;
            break;
        case 'K':
            board->white_kings ^= from_mask;
            board->white_kings ^= to_mask;
            break;
    }

    if (from_piece == 'K') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->white_rooks ^= (1ULL << (7 * 8 + 5));
                board->white_rooks ^= (1ULL << (7 * 8 + 7));
                hash = update_hash_with_piece_white(hash, 7 * 8 + 7, 'R');
                hash = update_hash_with_piece_white(hash, 7 * 8 + 5, 'R');
                hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
                if (board->white_left_castling) {
                    hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
                }
            } else if (to_x == 2) {
                board->white_rooks ^= (1ULL << (7 * 8 + 0));
                board->white_rooks ^= (1ULL << (7 * 8 + 3));
                hash = update_hash_with_piece_white(hash, 7 * 8 + 0, 'R');
                hash = update_hash_with_piece_white(hash, 7 * 8 + 3, 'R');
                hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
                if (board->white_right_castling) {
                    hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
                }
            }
        }
        board->white_right_castling = 0;
        board->white_left_castling = 0;
    } else {
        if (from_y == 7) {
            if (from_x == 0) {
                if (board->white_left_castling) {
                    board->white_left_castling = 0;
                    hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
                }
            } else if (from_x == 7) {
                if (board->white_right_castling) {
                    board->white_right_castling = 0;
                    hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
                }
            }
        }
    }
    if (to_y == 0) {
        if (to_x == 0) {
            if (board->black_left_castling) {
                board->black_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
            }
        } else if (to_x == 7) {
            if (board->black_right_castling) {
                board->black_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
            }
        }
    }

    hash = update_hash_with_piece(hash, to_p, from_piece);

    hash ^= zobrist_depth[depth + 1];

    board->color = BLACK_COLOR;

    *out_hash = hash;
    return score;
}

static int just_play_black_pawn(board_t * board, const play_t * play, int score, int depth, int64_t * out_hash) {
    int from_x = play->from_x;
    int from_y = play->from_y;
    int to_x = play->to_x;
    int to_y = play->to_y;
    int en_passant_x = board->en_passant_x;
    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;
    char promotion_option = play->promotion_option;

    char from_piece = 'p';
    char to_piece = identify_piece_white(board, to_p);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_black(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_white(hash, to_p, to_piece);
    }

    hash ^= zobrist_depth[depth];
    hash ^= zobrist_depth[depth + 1];

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'P':
            board->white_pawns ^= to_mask;
            score -= 1 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'N':
            board->white_knights ^= to_mask;
            score -= 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'B':
            board->white_bishops ^= to_mask;
            score -= 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'R':
            board->white_rooks ^= to_mask;
            score -= 5 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'Q':
            board->white_queens ^= to_mask;
            score -= 9 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'K':
            board->white_kings ^= to_mask;
            break;
    }

    board->black_pawns ^= from_mask;

    if (en_passant_x != NO_EN_PASSANT) {
        if (en_passant_x == to_x && from_y == 4) {
            hash = update_hash_with_piece_white(hash, 4 * 8 + en_passant_x, 'P');
            board->white_pawns ^= to_mask;
            score += 1 * PIECE_SCORE_MULTIPLIER;
        }
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    if (to_y == 7) {
        if (to_x == 0) {
            if (board->white_left_castling) {
                board->white_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
            }
        } else if (to_x == 7) {
            if (board->white_right_castling) {
                board->white_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
            }
        }

        if (promotion_option == PROMOTION_QUEEN) {
            // 1 to 9
            score -= 8 * PIECE_SCORE_MULTIPLIER;
            board->black_queens ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'q');
        } else if (promotion_option == PROMOTION_KNIGHT) {
            // 1 to 3
            score -= 2 * PIECE_SCORE_MULTIPLIER;
            board->black_knights ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'n');
        } else if (promotion_option == PROMOTION_BISHOP) {
            // 1 to 3
            score -= 2 * PIECE_SCORE_MULTIPLIER;
            board->black_bishops ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'b');
        } else {
            // 1 to 5
            score -= 4 * PIECE_SCORE_MULTIPLIER;
            board->black_rooks ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'r');
        }
    } else {
        board->black_pawns ^= to_mask;
        hash = update_hash_with_piece(hash, to_p, 'p');

        if (from_y == 1 && to_y == 3) {
            board->en_passant_x = from_x;
            hash ^= zobrist_en_passant[from_x];
        }
    }

    board->color = WHITE_COLOR;

    *out_hash = hash;
    return score;
}

static int just_play_black_complex(board_t * board, const play_t * play, int score, int depth, int64_t * out_hash) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    int from_p = from_y * 8 + from_x;

    char from_piece = identify_piece_black(board, from_p);
    if (from_piece == 'p') {
        return just_play_black_pawn(board, play, score, depth, out_hash);
    }

    char to_x = play->to_x;
    char to_y = play->to_y;
    int to_p = to_y * 8 + to_x;
    int en_passant_x = board->en_passant_x;

    char to_piece = identify_piece_white(board, to_p);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_black(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_white(hash, to_p, to_piece);
    }

    if (en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    hash ^= zobrist_depth[depth];

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'P':
            board->white_pawns ^= to_mask;
            score -= 1 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'N':
            board->white_knights ^= to_mask;
            score -= 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'B':
            board->white_bishops ^= to_mask;
            score -= 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'R':
            board->white_rooks ^= to_mask;
            score -= 5 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'Q':
            board->white_queens ^= to_mask;
            score -= 9 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'K':
            board->white_kings ^= to_mask;
            break;
    }
    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'n':
            board->black_knights ^= from_mask;
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= from_mask;
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= from_mask;
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= from_mask;
            board->black_queens ^= to_mask;
            break;
        case 'k':
            board->black_kings ^= from_mask;
            board->black_kings ^= to_mask;
            break;
    }

    if (from_piece == 'k') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->black_rooks ^= (1ULL << (0 * 8 + 5));
                board->black_rooks ^= (1ULL << (0 * 8 + 7));
                hash = update_hash_with_piece_black(hash, 0 * 8 + 7, 'r');
                hash = update_hash_with_piece_black(hash, 0 * 8 + 5, 'r');
                hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
                if (board->black_left_castling) {
                    hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
                }
            } else if (to_x == 2) {
                board->black_rooks ^= (1ULL << (0 * 8 + 0));
                board->black_rooks ^= (1ULL << (0 * 8 + 3));
                hash = update_hash_with_piece_black(hash, 0 * 8 + 0, 'r');
                hash = update_hash_with_piece_black(hash, 0 * 8 + 3, 'r');
                hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
                if (board->black_right_castling) {
                    hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
                }
            }
        }
        board->black_right_castling = 0;
        board->black_left_castling = 0;
    } else {
        if (from_y == 0) {
            if (from_x == 0) {
                if (board->black_left_castling) {
                    board->black_left_castling = 0;
                    hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
                }
            } else if (from_x == 7) {
                if (board->black_right_castling) {
                    board->black_right_castling = 0;
                    hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
                }
            }
        }
    }
    if (to_y == 7) {
        if (to_x == 0) {
            if (board->white_left_castling) {
                board->white_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
            }
        } else if (to_x == 7) {
            if (board->white_right_castling) {
                board->white_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
            }
        }
    }

    hash = update_hash_with_piece(hash, to_p, from_piece);

    hash ^= zobrist_depth[depth + 1];

    board->color = WHITE_COLOR;

    *out_hash = hash;
    return score;
}

static void actual_play(const play_t * play) {
    board_to_short_string(past_positions[past_plays_count++], &board);

    char moving_piece = identify_piece(&board, play->from_y * 8 + play->from_x);
    if (moving_piece == 'P' || moving_piece == 'p' || identify_piece(&board, play->to_y * 8 + play->to_x) != ' ') {
        board.halfmoves = 0;
    } else {
        board.halfmoves++;
    }

    last_play_x = play->to_x;
    last_play_y = play->to_y;
    if (board.color == BLACK_COLOR) {
        fullmoves++;
    }

    int64_t hash = 0;
    if (board.color == WHITE_COLOR) {
        just_play_white_complex(&board, play, 0, 0, &hash);
    } else {
        just_play_black_complex(&board, play, 0, 0, &hash);
    }
}

static int enumerate_all_possible_plays_white(play_t * valid_plays, const board_t * board) {
    int valid_plays_i = 0;

    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t empty_mask = ~(white_mask | black_mask);
    uint64_t moves = (board->white_pawns >> 8) & empty_mask;

    // Pawn single move forward and promotion
    while (moves) {
        int to = __builtin_ctzll(moves);
        int from = to + 8;

        int to_x = to % 8;
        int to_y = to / 8;
        int from_x = from % 8;
        int from_y = from / 8;

        if (to_y == 0) {
            valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
        } else {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
        }

        moves &= moves - 1;
    }

    // Pawn double move forward
    uint64_t single = (board->white_pawns >> 8) & empty_mask;
    moves = ((single >> 8) & empty_mask) & 0x000000FF00000000ULL;

    while (moves) {
        int to = __builtin_ctzll(moves);
        int from = to + 16;

        int to_x = to % 8;
        int to_y = to / 8;
        int from_x = from % 8;
        int from_y = from / 8;

        valid_plays[valid_plays_i].promotion_option = 0;
        valid_plays[valid_plays_i].from_x = from_x;
        valid_plays[valid_plays_i].from_y = from_y;
        valid_plays[valid_plays_i].to_x = to_x;
        valid_plays[valid_plays_i].to_y = to_y;
        valid_plays_i = valid_plays_i + 1;

        moves &= moves - 1;
    }

    // Pawn captures
    moves = board->white_pawns;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = white_pawn_capture_masks[from] & black_mask;
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            if (to_y == 0) {
                valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
            } else {
                valid_plays[valid_plays_i].promotion_option = 0;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
            }

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // Pawn captures via en passant
    if (board->en_passant_x != NO_EN_PASSANT) {
        moves = white_en_passant_capture_masks[(int)board->en_passant_x] & board->white_pawns;
        while (moves) {
            int from = __builtin_ctzll(moves);
            int from_x = from % 8;
            int from_y = from / 8;
            int to_x = board->en_passant_x;
            int to_y = 2;

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves &= moves - 1;
        }
    }

    // Knight moves
    moves = board->white_knights;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = knight_moves_masks[from] & (empty_mask | black_mask);
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // King moves
    moves = board->white_kings;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = king_moves_masks[from] & (empty_mask | black_mask);
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // King castling
    if (board->white_kings & (1ULL << (7 * 8 + 4))) {
        if (board->white_left_castling && (empty_mask & (1ULL << (7 * 8 + 1))) && (empty_mask & (1ULL << (7 * 8 + 2))) && (empty_mask & (1ULL << (7 * 8 + 3)))) {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = 4;
            valid_plays[valid_plays_i].from_y = 7;
            valid_plays[valid_plays_i].to_x = 2;
            valid_plays[valid_plays_i].to_y = 7;
            valid_plays_i = valid_plays_i + 1;
        }
        if (board->white_right_castling && (empty_mask & (1ULL << (7 * 8 + 5))) && (empty_mask & (1ULL << (7 * 8 + 6)))) {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = 4;
            valid_plays[valid_plays_i].from_y = 7;
            valid_plays[valid_plays_i].to_x = 6;
            valid_plays[valid_plays_i].to_y = 7;
            valid_plays_i = valid_plays_i + 1;
        }
    }

    // Rooks and queens
    moves = board->white_rooks | board->white_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & white_mask) {
                    break;
                }

                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = x;
                valid_plays[valid_plays_i].to_y = y;
                valid_plays_i++;

                if (to_mask & black_mask) {
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    // Bishops and queens
    moves = board->white_bishops | board->white_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,1}, {-1,1}, {1,-1}, {-1,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & white_mask) {
                    break;
                }

                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = x;
                valid_plays[valid_plays_i].to_y = y;
                valid_plays_i++;

                if (to_mask & black_mask) {
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    return valid_plays_i;
}

static int enumerate_all_possible_plays_black(play_t * valid_plays, const board_t * board) {
    int valid_plays_i = 0;

    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t empty_mask = ~(white_mask | black_mask);
    uint64_t moves = (board->black_pawns << 8) & empty_mask;

    // Pawn single move forward and promotion
    while (moves) {
        int to = __builtin_ctzll(moves);
        int from = to - 8;

        int to_x = to % 8;
        int to_y = to / 8;
        int from_x = from % 8;
        int from_y = from / 8;

        if (to_y == 7) {
            valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
        } else {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
        }

        moves &= moves - 1;
    }

    // Pawn double move forward
    uint64_t single = (board->black_pawns << 8) & empty_mask;
    moves = ((single << 8) & empty_mask) & 0x00000000FF000000ULL;

    while (moves) {
        int to = __builtin_ctzll(moves);
        int from = to - 16;

        int to_x = to % 8;
        int to_y = to / 8;
        int from_x = from % 8;
        int from_y = from / 8;

        valid_plays[valid_plays_i].promotion_option = 0;
        valid_plays[valid_plays_i].from_x = from_x;
        valid_plays[valid_plays_i].from_y = from_y;
        valid_plays[valid_plays_i].to_x = to_x;
        valid_plays[valid_plays_i].to_y = to_y;
        valid_plays_i = valid_plays_i + 1;

        moves &= moves - 1;
    }

    // Pawn captures
    moves = board->black_pawns;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = white_pawn_capture_masks[from] & white_mask;
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            if (to_y == 7) {
                valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
            } else {
                valid_plays[valid_plays_i].promotion_option = 0;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
            }

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // Pawn captures via en passant
    if (board->en_passant_x != NO_EN_PASSANT) {
        moves = black_en_passant_capture_masks[(int)board->en_passant_x] & board->black_pawns;
        while (moves) {
            int from = __builtin_ctzll(moves);
            int from_x = from % 8;
            int from_y = from / 8;
            int to_x = board->en_passant_x;
            int to_y = 5;

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves &= moves - 1;
        }
    }

    // Knight moves
    moves = board->black_knights;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = knight_moves_masks[from] & (empty_mask | white_mask);
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // King moves
    moves = board->black_kings;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = king_moves_masks[from] & (empty_mask | white_mask);
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // King castling
    if (board->black_kings & (1ULL << (0 * 8 + 4))) {
        if (board->black_left_castling && (empty_mask & (1ULL << (0 * 8 + 1))) && (empty_mask & (1ULL << (0 * 8 + 2))) && (empty_mask & (1ULL << (0 * 8 + 3)))) {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = 4;
            valid_plays[valid_plays_i].from_y = 0;
            valid_plays[valid_plays_i].to_x = 2;
            valid_plays[valid_plays_i].to_y = 0;
            valid_plays_i = valid_plays_i + 1;
        }
        if (board->black_right_castling && (empty_mask & (1ULL << (0 * 8 + 5))) && (empty_mask & (1ULL << (0 * 8 + 6)))) {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = 4;
            valid_plays[valid_plays_i].from_y = 0;
            valid_plays[valid_plays_i].to_x = 6;
            valid_plays[valid_plays_i].to_y = 0;
            valid_plays_i = valid_plays_i + 1;
        }
    }

    // Rooks and queens
    moves = board->black_rooks | board->black_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & black_mask) {
                    break;
                }

                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = x;
                valid_plays[valid_plays_i].to_y = y;
                valid_plays_i++;

                if (to_mask & white_mask) {
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    // Bishops and queens
    moves = board->black_bishops | board->black_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,1}, {-1,1}, {1,-1}, {-1,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & black_mask) {
                    break;
                }

                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = x;
                valid_plays[valid_plays_i].to_y = y;
                valid_plays_i++;

                if (to_mask & white_mask) {
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    return valid_plays_i;
}

// Ignores en passant and king captures
static uint64_t mask_attacked_positions_by_white(const board_t * board) {
    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t attacked = 0;

    // Pawn captures
    uint64_t moves = board->white_pawns;
    while (moves) {
        int from = __builtin_ctzll(moves);

        uint64_t moves_to = white_pawn_capture_masks[from] & black_mask;
        attacked |= moves_to;

        moves &= moves - 1;
    }

    // Knight captures
    moves = board->white_knights;
    while (moves) {
        int from = __builtin_ctzll(moves);

        uint64_t moves_to = knight_moves_masks[from] & black_mask;
        attacked |= moves_to;

        moves &= moves - 1;
    }

    // King captures
    moves = board->white_kings;
    while (moves) {
        int from = __builtin_ctzll(moves);

        uint64_t moves_to = king_moves_masks[from] & black_mask;
        attacked |= moves_to;

        moves &= moves - 1;
    }

    // Rooks and queens
    moves = board->white_rooks | board->white_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & white_mask) {
                    break;
                }

                if (to_mask & black_mask) {
                    attacked |= to_mask;
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    // Bishops and queens
    moves = board->white_bishops | board->white_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,1}, {-1,1}, {1,-1}, {-1,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & white_mask) {
                    break;
                }

                if (to_mask & black_mask) {
                    attacked |= to_mask;
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    return attacked;
}

// Ignores en passant and king captures
static uint64_t mask_attacked_positions_by_black(const board_t * board) {
    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t attacked = 0;

    // Pawn captures
    uint64_t moves = board->black_pawns;
    while (moves) {
        int from = __builtin_ctzll(moves);

        uint64_t moves_to = black_pawn_capture_masks[from] & white_mask;
        attacked |= moves_to;

        moves &= moves - 1;
    }

    // Knight captures
    moves = board->black_knights;
    while (moves) {
        int from = __builtin_ctzll(moves);

        uint64_t moves_to = knight_moves_masks[from] & white_mask;
        attacked |= moves_to;

        moves &= moves - 1;
    }

    // King captures
    moves = board->black_kings;
    while (moves) {
        int from = __builtin_ctzll(moves);

        uint64_t moves_to = king_moves_masks[from] & white_mask;
        attacked |= moves_to;

        moves &= moves - 1;
    }

    // Rooks and queens
    moves = board->black_rooks | board->black_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & black_mask) {
                    break;
                }

                if (to_mask & white_mask) {
                    attacked |= to_mask;
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    // Bishops and queens
    moves = board->black_bishops | board->black_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        int directions[4][2] = { {1,1}, {-1,1}, {1,-1}, {-1,-1} };
        for (int d = 0; d < 4; d++) {
            int dx = directions[d][0];
            int dy = directions[d][1];
            int x = from_x + dx;
            int y = from_y + dy;

            while (x >= 0 && x < 8 && y >= 0 && y < 8) {
                int to = y * 8 + x;
                uint64_t to_mask = 1ULL << to;

                if (to_mask & black_mask) {
                    break;
                }

                if (to_mask & white_mask) {
                    attacked |= to_mask;
                    break;
                }

                x += dx;
                y += dy;
            }
        }

        moves &= moves - 1;
    }

    return attacked;
}

static int enumerate_legal_plays_white(play_t * valid_plays, const board_t * board) {
    int valid_plays_i = 0;
    play_t valid_plays_local[128];
    int valid_plays_local_i = enumerate_all_possible_plays_white(valid_plays_local, board);
    char king_piece = 'K';
    char castling_possible = board->white_left_castling || board->white_right_castling;

    board_t board_cpy;

    // Detect if playing exposes king to immediate capture (illegal move)
    for (int i = 0; i < valid_plays_local_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));
        just_play_white_simple(&board_cpy, &valid_plays_local[i]);

        uint64_t attacked = mask_attacked_positions_by_black(&board_cpy);
        if (attacked & board_cpy.white_kings) {
            continue;
        }

        if (castling_possible) {
            int from_x = valid_plays_local[i].from_x;
            int from_y = valid_plays_local[i].from_y;
            char piece = identify_piece_white(board, from_y * 8 + from_x);
            if (piece == king_piece && from_x == 4) {
                int to_x = valid_plays_local[i].to_x;

                if (to_x == 6) {
                    if (attacked & (7ULL << (from_y * 8 + 5))) {
                        continue;
                    }
                } else if (to_x == 2) {
                    if (attacked & (7ULL << (from_y * 8 + 3))) {
                        continue;
                    }
                }
            }
        }

        valid_plays[valid_plays_i] = valid_plays_local[i];
        valid_plays_i++;
    }

    return valid_plays_i;
}

static int enumerate_legal_plays_black(play_t * valid_plays, const board_t * board) {
    int valid_plays_i = 0;
    play_t valid_plays_local[128];
    int valid_plays_local_i = enumerate_all_possible_plays_black(valid_plays_local, board);
    char king_piece = 'k';
    char castling_possible = board->black_left_castling || board->black_right_castling;

    board_t board_cpy;

    // Detect if playing exposes king to immediate capture (illegal move)
    for (int i = 0; i < valid_plays_local_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));
        just_play_black_simple(&board_cpy, &valid_plays_local[i]);

        uint64_t attacked = mask_attacked_positions_by_white(&board_cpy);
        if (attacked & board_cpy.black_kings) {
            continue;
        }

        if (castling_possible) {
            int from_x = valid_plays_local[i].from_x;
            int from_y = valid_plays_local[i].from_y;
            char piece = identify_piece_black(board, from_y * 8 + from_x);
            if (piece == king_piece && from_x == 4) {
                int to_x = valid_plays_local[i].to_x;

                if (to_x == 6) {
                    if (attacked & (7ULL << (from_y * 8 + 5))) {
                        continue;
                    }
                } else if (to_x == 2) {
                    if (attacked & (7ULL << (from_y * 8 + 3))) {
                        continue;
                    }
                }
            }
        }

        valid_plays[valid_plays_i] = valid_plays_local[i];
        valid_plays_i++;
    }

    return valid_plays_i;
}

static int enumerate_legal_plays(play_t * valid_plays, const board_t * board) {
    if (board->color == WHITE_COLOR) {
        return enumerate_legal_plays_white(valid_plays, board);
    } else {
        return enumerate_legal_plays_black(valid_plays, board);
    }
}

static int king_threatened_white(board_t * board) {
    uint64_t king_mask = board->white_kings ;

    char en_passant_x = board->en_passant_x;
    char white_left_castling = board->white_left_castling;
    char white_right_castling = board->white_right_castling;
    char black_left_castling = board->black_left_castling;
    char black_right_castling = board->black_right_castling;

    board->en_passant_x = NO_EN_PASSANT;
    board->white_left_castling = 0;
    board->white_right_castling = 0;
    board->black_left_castling = 0;
    board->black_right_castling = 0;
    board->color = BLACK_COLOR;

    uint64_t attacked = mask_attacked_positions_by_black(board);

    board->en_passant_x = en_passant_x;
    board->white_left_castling = white_left_castling;
    board->white_right_castling = white_right_castling;
    board->black_left_castling = black_left_castling;
    board->black_right_castling = black_right_castling;
    board->color = WHITE_COLOR;

    return attacked & king_mask;
}

static int king_threatened_black(board_t * board) {
    uint64_t king_mask = board->black_kings;

    char en_passant_x = board->en_passant_x;
    char white_left_castling = board->white_left_castling;
    char white_right_castling = board->white_right_castling;
    char black_left_castling = board->black_left_castling;
    char black_right_castling = board->black_right_castling;

    board->en_passant_x = NO_EN_PASSANT;
    board->white_left_castling = 0;
    board->white_right_castling = 0;
    board->black_left_castling = 0;
    board->black_right_castling = 0;
    board->color = WHITE_COLOR;

    uint64_t attacked = mask_attacked_positions_by_white(board);

    board->en_passant_x = en_passant_x;
    board->white_left_castling = white_left_castling;
    board->white_right_castling = white_right_castling;
    board->black_left_castling = black_left_castling;
    board->black_right_castling = black_right_castling;
    board->color = BLACK_COLOR;

    return attacked & king_mask;
}

static int king_threatened(board_t * board) {
    if (board->color == WHITE_COLOR) {
        return king_threatened_white(board);
    } else {
        return king_threatened_black(board);
    }
}

static int minimax_black(board_t * board, int depth, int alpha, int beta, int initial_score, int64_t hash);

static int minimax_white(board_t * board, int depth, int alpha, int beta, int initial_score, int64_t hash) {
    if (board->halfmoves == 50) {
        return -10000000 - depth * 128;
    }

    int alpha_orig = alpha;
    int beta_orig = beta;

    int score_w_type;
    int found = hash_table_find(&score_w_type, hash);
    if (found) {
        int score = score_w_type >> 2;
        int type = score_w_type & 3;

        if (type == TYPE_EXACT) {
            return score;
        } else if (type == TYPE_LOWER_BOUND && score > alpha) {
            alpha = score;
        } else if (type == TYPE_UPPER_BOUND && score < beta) {
            beta = score;
        }

        if (alpha >= beta) {
            return score;
        }
    }

    if (depth == MAX_SEARCH_DEPTH) {
        if (!found) {
            int score_w_type = (initial_score << 2) | TYPE_EXACT;
            hash_table_insert(hash, score_w_type);
        }
        return initial_score;
    }

    board_t board_cpy;
    play_t valid_plays[128];
    char captures[128];

    int valid_plays_i = enumerate_legal_plays_white(valid_plays, board);
    if (valid_plays_i == 0) {
        if (king_threatened_white(board)) {
            int score = -20000000 + depth * 128;

            if (!found) {
                int score_w_type = (score << 2) | TYPE_EXACT;
                hash_table_insert(hash, score_w_type);
            }
            return score;
        } else {
            int score = -10000000 - depth * 128;

            if (!found) {
                int score_w_type = (score << 2) | TYPE_EXACT;
                hash_table_insert(hash, score_w_type);
            }
            return score;
        }
    }

    int best_score = NO_SCORE;
    int breakfor = 0;

    for (int i = 0; i < valid_plays_i; ++i) {
        captures[i] = identify_piece_black(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x) != ' ';
        if (!captures[i]) {
            continue;
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        int score = just_play_white_complex(&board_cpy, &valid_plays[i], initial_score, depth, &this_hash);
        int score_extra = valid_plays_i;

        score = minimax_black(&board_cpy, depth + 1, alpha, beta, score + score_extra, this_hash);

        if (score != NO_SCORE) {
            if (best_score == NO_SCORE || score > best_score) {
                best_score = score;
            }
            if (score >= beta) {
                breakfor = 1;
                break;
            }
            alpha = MAX(alpha, score);
        }
    }

    if (!breakfor) {
        for (int i = 0; i < valid_plays_i; ++i) {
            if (captures[i]) {
                continue;
            }

            memcpy(&board_cpy, board, sizeof(board_t));
            int64_t this_hash = hash;

            int score = just_play_white_complex(&board_cpy, &valid_plays[i], initial_score, depth, &this_hash);
            int score_extra = valid_plays_i;

            score = minimax_black(&board_cpy, depth + 1, alpha, beta, score + score_extra, this_hash);

            if (score != NO_SCORE) {
                if (best_score == NO_SCORE || score > best_score) {
                    best_score = score;
                }
                if (score >= beta) {
                    break;
                }
                alpha = MAX(alpha, score);
            }
        }
    }

    if (!found) {
        int type;
        if (best_score <= alpha_orig) {
            type = TYPE_UPPER_BOUND;
        } else if (best_score >= beta_orig) {
            type = TYPE_LOWER_BOUND;
        } else {
            type = TYPE_EXACT;
        }

        int score_w_type = (best_score << 2) | type;
        hash_table_insert(hash, score_w_type);
    }

    return best_score;
}

static int minimax_black(board_t * board, int depth, int alpha, int beta, int initial_score, int64_t hash) {
    if (board->halfmoves == 50) {
        return 10000000 + depth * 128;
    }

    int alpha_orig = alpha;
    int beta_orig = beta;

    int score_w_type;
    int found = hash_table_find(&score_w_type, hash);
    if (found) {
        int score = score_w_type >> 2;
        int type = score_w_type & 3;

        if (type == TYPE_EXACT) {
            return score;
        } else if (type == TYPE_LOWER_BOUND && score > alpha) {
            alpha = score;
        } else if (type == TYPE_UPPER_BOUND && score < beta) {
            beta = score;
        }

        if (alpha >= beta) {
            return score;
        }
    }

    if (depth == MAX_SEARCH_DEPTH) {
        if (!found) {
            int score_w_type = (initial_score << 2) | TYPE_EXACT;
            hash_table_insert(hash, score_w_type);
        }
        return initial_score;
    }

    board_t board_cpy;
    play_t valid_plays[128];
    char captures[128];

    int valid_plays_i = enumerate_legal_plays_black(valid_plays, board);
    if (valid_plays_i == 0) {
        if (king_threatened_black(board)) {
            int score = 20000000 - depth * 128;

            if (!found) {
                int score_w_type = (score << 2) | TYPE_EXACT;
                hash_table_insert(hash, score_w_type);
            }
            return score;
        } else {
            int score = 10000000 + depth * 128;

            if (!found) {
                int score_w_type = (score << 2) | TYPE_EXACT;
                hash_table_insert(hash, score_w_type);
            }
            return score;
        }
    }

    int best_score = NO_SCORE;
    int breakfor = 0;

    for (int i = 0; i < valid_plays_i; ++i) {
        captures[i] = identify_piece_white(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x) != ' ';
        if (!captures[i]) {
            continue;
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        int score = just_play_black_complex(&board_cpy, &valid_plays[i], initial_score, depth, &this_hash);
        int score_extra = -valid_plays_i;

        score = minimax_white(&board_cpy, depth + 1, alpha, beta, score + score_extra, this_hash);

        if (score != NO_SCORE) {
            if (best_score == NO_SCORE || score < best_score) {
                best_score = score;
            }
            if (score <= alpha) {
                breakfor = 1;
                break;
            }
            beta = MIN(beta, score);
        }
    }

    if (!breakfor) {
        for (int i = 0; i < valid_plays_i; ++i) {
            if (captures[i]) {
                continue;
            }

            memcpy(&board_cpy, board, sizeof(board_t));
            int64_t this_hash = hash;

            int score = just_play_black_complex(&board_cpy, &valid_plays[i], initial_score, depth, &this_hash);
            int score_extra = -valid_plays_i;

            score = minimax_white(&board_cpy, depth + 1, alpha, beta, score + score_extra, this_hash);

            if (score != NO_SCORE) {
                if (best_score == NO_SCORE || score < best_score) {
                    best_score = score;
                }
                if (score <= alpha) {
                    break;
                }
                beta = MIN(beta, score);
            }
        }
    }

    if (!found) {
        int type;
        if (best_score <= alpha_orig) {
            type = TYPE_UPPER_BOUND;
        } else if (best_score >= beta_orig) {
            type = TYPE_LOWER_BOUND;
        } else {
            type = TYPE_EXACT;
        }

        int score_w_type = (best_score << 2) | type;
        hash_table_insert(hash, score_w_type);
    }

    return best_score;
}

static int ai_play(play_t * play) {
    if (opening_book_enabled) {
        uint64_t board_hash = hash_from_board(&board, 0);
        int play_found = get_opening_book_play(play, board_hash);
        if (play_found) {
            actual_play(play);
            return 1;
        }
    }

    play_t valid_plays[128];
    board_t board_cpy;
    int alpha = -2147483644;
    int beta = 2147483644;

    int valid_plays_i = enumerate_legal_plays(valid_plays, &board);
    if (valid_plays_i == 0) {
        if (king_threatened(&board)) {
            return CHECK_MATE;
        } else {
            return DRAW;
        }
    }

    int best_score = NO_SCORE;
    int best_play = 0;
    bzero(hash_table, HASH_TABLE_SIZE * sizeof(hash_table_entry_t));
    int64_t hash = 0;

    for (int i = 0; i < valid_plays_i; ++i) {
        memcpy(&board_cpy, &board, sizeof(board_t));

        int score = board_cpy.color == WHITE_COLOR ? just_play_white_complex(&board_cpy, &valid_plays[i], 0, 0, &hash) : just_play_black_complex(&board_cpy, &valid_plays[i], 0, 0, &hash);
        int score_extra = board.color == WHITE_COLOR ? valid_plays_i : -valid_plays_i;

        board_to_short_string(buffer, &board_cpy);

        int position_repeated = 0;
        for (int pos = 0; pos < past_plays_count; ++pos) {
            if (strcmp(past_positions[pos], buffer) == 0) {
                position_repeated++;
                if (position_repeated == 2) {
                    break;
                }
            }
        }
        if (position_repeated == 2) {
            score = board_cpy.color == WHITE_COLOR ? 10000000 + 5 * 128 : -10000000 - 5 * 128;
        } else {
            if (board_cpy.color == WHITE_COLOR) {
                score = minimax_white(&board_cpy, 0, alpha, beta, score + score_extra, hash_from_board(&board_cpy, 0));
            } else {
                score = minimax_black(&board_cpy, 0, alpha, beta, score + score_extra, hash_from_board(&board_cpy, 0));
            }
        }

        if (score != NO_SCORE) {
            if (board.color == WHITE_COLOR) {
                if (best_score == NO_SCORE || score > best_score) {
                    best_score = score;
                    best_play = i;
                }
                if (score >= beta) {
                    break;
                }
                alpha = MAX(alpha, score);
            } else {
                if (best_score == NO_SCORE || score < best_score) {
                    best_score = score;
                    best_play = i;
                }
                if (score <= alpha) {
                    break;
                }
                beta = MIN(beta, score);
            }
        }
    }

    if (best_score == NO_SCORE) {
        if (king_threatened(&board)) {
            return CHECK_MATE;
        } else {
            return DRAW;
        }
    }

    actual_play(&valid_plays[best_play]);
    *play = valid_plays[best_play];
    return 1;
}

static char input_promotion_piece() {
    while (1) {
        printf("Promotion choice (options: Q, N, B, R): ");
        fgets(buffer, 1024, stdin);

        if (buffer[0] == 'q' || buffer[0] == 'Q') {
            return PROMOTION_QUEEN;
        }
        if (buffer[0] == 'n' || buffer[0] == 'N') {
            return PROMOTION_KNIGHT;
        }
        if (buffer[0] == 'b' || buffer[0] == 'B') {
            return PROMOTION_BISHOP;
        }
        if (buffer[0] == 'r' || buffer[0] == 'R') {
            return PROMOTION_ROOK;
        }
    }
}

static char * read_play(play_t * play, char * str) {
    if (str[0] >= 'a' && str[0] <= 'h') {
        play->from_x = str[0] - 'a';
    } else {
        return NULL;
    }
    if (str[1] >= '1' && str[1] <= '8') {
        play->from_y = 8 - (str[1] - '0');
    } else {
        return NULL;
    }
    if (str[2] >= 'a' && str[2] <= 'h') {
        play->to_x = str[2] - 'a';
    } else {
        return NULL;
    }
    if (str[3] >= '1' && str[3] <= '8') {
        play->to_y = 8 - (str[3] - '0');
    } else {
        return NULL;
    }
    if (str[4] == 'q') {
        play->promotion_option = PROMOTION_QUEEN;
    }
    if (str[4] == 'n') {
        play->promotion_option = PROMOTION_KNIGHT;
    }
    if (str[4] == 'b') {
        play->promotion_option = PROMOTION_BISHOP;
    }
    if (str[4] == 'r') {
        play->promotion_option = PROMOTION_ROOK;
    }
    if (str[4] == 0) {
        return str + 4;
    }
    if (str[4] == ' ') {
        return str + 5;
    }
    if (str[5] == 0) {
        return str + 5;
    }
    if (str[5] == ' ') {
        return str + 6;
    }
    if (str[6] == ' ') {
        return str + 7;
    }
    return NULL;
}

static void input_play(play_t * play, const play_t * valid_plays, int valid_plays_i) {
    while (1) {
        printf("Input (example: e2e4): ");
        fgets(buffer, 1024, stdin);

        buffer[4] = 0;
        char * input_is_valid = read_play(play, buffer);

        if (input_is_valid) {
            int play_is_valid = 0;
            for (int i = 0; i < valid_plays_i; ++i) {
                char from_x2 = valid_plays[i].from_x;
                char from_y2 = valid_plays[i].from_y;
                char to_x2 = valid_plays[i].to_x;
                char to_y2 = valid_plays[i].to_y;
                if (play->from_x == from_x2 && play->from_y == from_y2 && play->to_x == to_x2 && play->to_y == to_y2) {
                    play_is_valid = 1;
                    break;
                }
            }
            if (play_is_valid) {
                char from_piece = identify_piece(&board, play->from_y * 8 + play->from_x);
                play->promotion_option = 0;
                if (play->from_y == 1 && play->to_y == 0 && from_piece == 'P') {
                    play->promotion_option = input_promotion_piece();
                } else if (play->from_y == 6 && play->to_y == 7 && from_piece == 'p') {
                    play->promotion_option = input_promotion_piece();
                }

                printf("\n");
                return;
            } else {
                printf("\nInvalid play.\n\n");
            }
        }
    }
}

static void reset_board() {
    const char * pieces = "rnbqkbnrpppppppp                                PPPPPPPPRNBQKBNR";

    board.white_pawns = 0;
    board.black_pawns = 0;
    board.white_knights = 0;
    board.black_knights = 0;
    board.white_bishops = 0;
    board.black_bishops = 0;
    board.white_rooks = 0;
    board.black_rooks = 0;
    board.white_queens = 0;
    board.black_queens = 0;
    board.white_kings = 0;
    board.black_kings = 0;

    for (int p = 0; p < 64; ++p) {
        uint64_t bit = 1ULL << p;

        switch (pieces[p]) {
            case 'P': board.white_pawns   |= bit; break;
            case 'p': board.black_pawns   |= bit; break;
            case 'N': board.white_knights |= bit; break;
            case 'n': board.black_knights |= bit; break;
            case 'B': board.white_bishops |= bit; break;
            case 'b': board.black_bishops |= bit; break;
            case 'R': board.white_rooks   |= bit; break;
            case 'r': board.black_rooks   |= bit; break;
            case 'Q': board.white_queens  |= bit; break;
            case 'q': board.black_queens  |= bit; break;
            case 'K': board.white_kings   |= bit; break;
            case 'k': board.black_kings   |= bit; break;
        }
    }

    board.en_passant_x = NO_EN_PASSANT;
    board.white_left_castling = 1;
    board.white_right_castling = 1;
    board.black_left_castling = 1;
    board.black_right_castling = 1;
    board.color = WHITE_COLOR;
    board.halfmoves = 0;
    fullmoves = 0;
    past_plays_count = 0;
}

static void send_uci_command(FILE * fd, const char * str) {
    fprintf(fd, "< %s\n", str);
    fflush(fd);
    printf("%s\n", str);
    fflush(stdout);
}

static void text_mode_play(int player_one_is_human, int player_two_is_human) {
    play_t valid_plays[128];

    while (1) {
        print_board(&board);
        printf("%s to play...\n\n", board.color == WHITE_COLOR ? "White" : "Black");

        if (board.white_kings == 0 || board.black_kings == 0) {
            printf("ERROR\n");
            exit(EXIT_FAILURE);
        }

        int player_is_human = board.color == WHITE_COLOR ? player_one_is_human : player_two_is_human;

        if (player_is_human) {
            int valid_plays_i = enumerate_legal_plays(valid_plays, &board);
            if (valid_plays_i == 0) {
                if (king_threatened(&board)) {
                    printf("Player lost.\n");
                } else {
                    printf("Game is drawn.\n");
                }
                break;
            }

            play_t play;
            input_play(&play, valid_plays, valid_plays_i);
            actual_play(&play);
        } else {
            play_t play;
            int played = ai_play(&play);

            if (played == CHECK_MATE) {
                printf("Player lost.\n");
                break;
            }
            if (played == DRAW) {
                printf("Game is drawn.\n");
                break;
            }
        }
    }
}

static void text_mode() {
    printf("Prawn %s\n\nYour color?\n 1 - White pieces\n 2 - Black pieaces\n\n", PROGRAM_VERSION);

    int player_one_is_human;
    int player_two_is_human;

    while (1) {
        fgets(buffer, 1024, stdin);

        if (strcmp(buffer, "1\n") == 0) {
            player_one_is_human = 1;
            player_two_is_human = 0;
            break;
        }
        if (strcmp(buffer, "2\n") == 0) {
            player_one_is_human = 0;
            player_two_is_human = 1;
            break;
        }
    }

    printf("\n");
    text_mode_play(player_one_is_human, player_two_is_human);
}

static void self_play() {
    printf("Prawn %s - self play mode\n\n", PROGRAM_VERSION);

    text_mode_play(0, 0);
}

static void uci_mode() {
    FILE * fd = fopen("uci.log", "a");
    fprintf(fd, "# Starting in UCI mode.\n");
    fflush(fd);

    while (1) {
        if (fgets(buffer, 1024, stdin) == NULL) {
            break;
        }
        for (int i = 0; i < 1024; ++i) {
            if (buffer[i] == '\n') {
                buffer[i] = 0;
                break;
            }
        }

        fprintf(fd, "> %s\n", buffer);
        fflush(fd);

        if (feof(fd) || ferror(fd) || strcmp(buffer, "quit") == 0) {
            break;
        }
        if (buffer[0] == '#') {
            continue;
        }
        if (strcmp(buffer, "uci") == 0) {
            sprintf(buffer, "id name prawn %s", PROGRAM_VERSION);
            send_uci_command(fd, buffer);
            send_uci_command(fd, "id author gonmf");
            send_uci_command(fd, "uciok");
            continue;
        }
        if (strcmp(buffer, "isready") == 0) {
            send_uci_command(fd, "readyok");
            continue;
        }
        if (strcmp(buffer, "ucinewgame") == 0) {
            reset_board();
            continue;
        }
        if (strncmp(buffer, "position ", strlen("position ")) == 0) {
            char * str = strstr(buffer, " startpos ");
            if (str) {
                reset_board();
            } else {
                str = strstr(buffer, " fen ");
                if (str) {
                    fen_to_board(&board, &fullmoves, str + strlen(" fen "));
                }
            }

            str = strstr(buffer, " moves ");
            if (str) {
                str += strlen(" moves ");

                while (str) {
                    play_t play;
                    str = read_play(&play, str);
                    if (str) {
                        actual_play(&play);
                    }
                }
            }
            continue;
        }
        if (strcmp(buffer, "go") == 0 || strncmp(buffer, "go ", strlen("go ")) == 0) {
            play_t play;
            int played = ai_play(&play);
            if (played == CHECK_MATE) {
                fprintf(fd, "# Player lost.\n");
                fflush(fd);
                continue;
            }
            if (played == DRAW) {
                fprintf(fd, "# Game is drawn.\n");
                fflush(fd);
                continue;
            }

            char * b = buffer + sprintf(buffer, "bestmove %c%d%c%d", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            char piece = identify_piece(&board, play.from_y * 8 + play.from_x);
            if (piece == 'P' && piece == 'p') {
                if (play.promotion_option == PROMOTION_QUEEN) {
                    sprintf(b, "q");
                } else if (play.promotion_option == PROMOTION_KNIGHT) {
                    sprintf(b, "n");
                } else if (play.promotion_option == PROMOTION_BISHOP) {
                    sprintf(b, "b");
                } else if (play.promotion_option == PROMOTION_ROOK) {
                    sprintf(b, "r");
                }
            }
            send_uci_command(fd, buffer);
            continue;
        }
    }

    fclose(fd);
}

static void show_help() {
    printf("Prawn %s\n\nOptions:\n", PROGRAM_VERSION);
    printf("  --position - Start from FEN position\n");
    printf("  --no-book  - Do not use the opening book\n");
    printf("  --uci      - Start on UCI intergace mode (default)\n");
    printf("  --text     - Play via the text interface\n");
    printf("  --self     - Have the program play against itself\n");
    printf("  --help     - Show this message\n\n");
}

int main(int argc, char * argv[]) {
    populate_pawn_capture_masks();
    populate_knight_moves_masks();
    populate_king_moves_masks();
    populate_zobrist_masks();
    hash_table = malloc(HASH_TABLE_SIZE * sizeof(hash_table_entry_t));

    int from_fen_idx = -1;

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
        }
    }

    if (opening_book_enabled) {
        init_opening_book();
    }

    reset_board();

    if (from_fen_idx != -1) {
        fen_to_board(&board, &fullmoves, argv[from_fen_idx]);
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--from-fen") == 0) {
            i++;
            continue;
        }
        if (strcmp(argv[i], "--no-book") == 0) {
            continue;
        }
        if (strcmp(argv[i], "--uci") == 0) {
            uci_mode();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--text") == 0) {
            text_mode();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--self") == 0) {
            self_play();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--help") == 0) {
            show_help();
            return EXIT_SUCCESS;
        }
        printf("Unrecognized argument \"%s\"\n", argv[i]);
        return EXIT_FAILURE;
    }

    uci_mode();
    return EXIT_SUCCESS;
}
