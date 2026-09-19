#include "common.h"

static char buffer[1024];

static board_t board;
static board_ext_t board_ext;

#define NO_SCORE -536870912
#define CHECK_MATE -536870911
#define DRAW 536870911

// A checkmate is worth MATE_SCORE less 128 for every ply it takes to reach, so that the search
// prefers the shortest one. Nothing the evaluation can produce comes near MATE_THRESHOLD, which
// makes it the test for "this score is a mate, not a material count".
#define MATE_SCORE 20000000
#define MATE_THRESHOLD (MATE_SCORE - 128 * (MAX_TOTAL_SEARCH_DEPTH + 1))

#define MAX(A,B) ((A) > (B) ? (A) : (B))
#define MIN(A,B) ((A) < (B) ? (A) : (B))

static uint64_t white_pawn_capture_masks[64];
static uint64_t black_pawn_capture_masks[64];
static uint64_t white_en_passant_capture_masks[8];
static uint64_t black_en_passant_capture_masks[8];
static uint64_t knight_moves_masks[64];
static uint64_t king_moves_masks[64];

static int64_t zobrist_map[64][12];
static int64_t zobrist_side_to_move; // hashes when black
static int64_t zobrist_en_passant[8];
static int64_t zobrist_castling[4];

static hash_table_entry_t * hash_table;

static struct timeval search_start;
static long search_budget_ms;
static int search_depth_limit = DEFAULT_SEARCH_DEPTH;
static int search_aborted;
static uint64_t search_nodes;
// Prevent stopping when running out of time in the first search iteration (of increasing depth).
static int search_abortable;

static long elapsed_ms(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000L + (end.tv_usec - start.tv_usec) / 1000L;
}

static long search_elapsed_ms() {
    struct timeval now;
    gettimeofday(&now, NULL);

    return elapsed_ms(search_start, now);
}

// Hashes of the positions the game has already been through, followed by the ones on the path to
// the node being searched. search_history_count is where the second part starts.
static int64_t search_history[256 + MAX_TOTAL_SEARCH_DEPTH + 4];
static int search_history_count;

// A position already seen is scored as a draw on its first repetition rather than its third: a side
// able to repeat once can nearly always repeat again, and waiting for the third costs plies to see.
// Only positions since the last pawn play or capture can repeat, which is what bounds the search.
static int is_repetition(int64_t hash, int depth, int halfmoves) {
    int limit = search_history_count + depth - halfmoves;
    if (limit < 0) {
        limit = 0;
    }

    for (int i = search_history_count + depth - 2; i >= limit; i -= 2) {
        if (search_history[i] == hash) {
            return 1;
        }
    }

    return 0;
}

static int out_of_time() {
    // avoid calling gettimeofday at every node
    if ((++search_nodes & 4095) != 0 || search_budget_ms == 0 || !search_abortable) {
        return 0;
    }

    return search_elapsed_ms() >= search_budget_ms;
}

static unsigned int opening_book_size;
static uint64_t opening_book[MAX_SUPPORTED_OB_RULES];
static char ob_play_colors[MAX_SUPPORTED_OB_RULES];
static play_short_t ob_plays[MAX_SUPPORTED_OB_RULES][4];

static int opening_book_enabled = 1;
static int extend_uci = 0;
static int arbitrate_draws = 1;
static int uci_game_in_error_state = 0;
static int convert_at_ob_depth = -1;

static void reset_board();
static void actual_play(board_t * board, board_ext_t * board_ext, const play_t * play);
static int64_t hash_from_board(const board_t * board);
static int enumerate_legal_plays(play_t * valid_plays, const board_t * board);
static int insufficient_material(const board_t * board);

static void init_opening_book() {
    opening_book_size = 0;

    play_t play;
    play.promotion_option = 0;
    play_t valid_plays[218];
    int valid_plays_i;
    int play_is_legal;
    int file_row = 0;

    play.promotion_option = 0;
    FILE * fp = fopen("openings.txt", "r");
    if (fp == NULL) {
        if (convert_at_ob_depth != -1) {
            fprintf(stderr, "Opening book openings.txt not found.\n");
            exit(EXIT_FAILURE);
        }

        fprintf(stderr, "Opening book openings.txt not found, playing without one.\n");
        opening_book_enabled = 0;
        return;
    }

    while (opening_book_size < MAX_SUPPORTED_OB_RULES) {
        file_row += 1;
        char * r = fgets(buffer, 4 * 1024, fp);
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
        int ob_depth = 1;
        char * token;
        while ((token = strsep(&s, " ")) != NULL && plays_found < 4) {
            if (token[0] == '|') {
                plays_found = 0;
                continue;
            }

            char color = board.color;
            play.from_x = token[0] - 'a';
            play.from_y = '8' - token[1];
            play.to_x = token[2] - 'a';
            play.to_y = '8' - token[3];

            valid_plays_i = enumerate_legal_plays(valid_plays, &board);
            play_is_legal = 0;
            for (int i = 0; i < valid_plays_i; ++i) {
                if (valid_plays[i].from_x == play.from_x && valid_plays[i].from_y == play.from_y && valid_plays[i].to_x == play.to_x && valid_plays[i].to_y == play.to_y && valid_plays[i].promotion_option == 0) {
                    play_is_legal = 1;
                    break;
                }
            }

            if (!play_is_legal) {
                fprintf(stderr, "Error reading openings book - invalid play @ line %d\n", file_row);
                exit(EXIT_FAILURE);
            }

            if (plays_found == -1) {
                actual_play(&board, &board_ext, &play);
                ob_depth++;
                continue;
            }

            ob_plays[opening_book_size][plays_found].from_x = play.from_x;
            ob_plays[opening_book_size][plays_found].from_y = play.from_y;
            ob_plays[opening_book_size][plays_found].to_x = play.to_x;
            ob_plays[opening_book_size][plays_found].to_y = play.to_y;
            ob_play_colors[opening_book_size] = color;
            plays_found++;
        }

        if (plays_found != 4) {
            fprintf(stderr, "Error loading opening book\n");
            exit(EXIT_FAILURE);
        }

        if (ob_depth == convert_at_ob_depth) {
            char fen_buf[100];
            board_t board_cpy;
            board_ext_t board_ext_cpy;
            for (int i = 0; i < 4; ++i) {
                memcpy(&board_cpy, &board, sizeof(board_t));
                memcpy(&board_ext_cpy, &board_ext, sizeof(board_ext_t));
                play.from_x = ob_plays[opening_book_size][i].from_x;
                play.from_y = ob_plays[opening_book_size][i].from_y;
                play.to_x = ob_plays[opening_book_size][i].to_x;
                play.to_y = ob_plays[opening_book_size][i].to_y;
                actual_play(&board_cpy, &board_ext_cpy, &play);
                board_to_fen(fen_buf, &board_cpy, &board_ext_cpy);
                printf("%s\n", fen_buf);
            }
        }

        opening_book[opening_book_size] = hash_from_board(&board);
        opening_book_size++;
    }

    if (opening_book_size == MAX_SUPPORTED_OB_RULES) {
        fprintf(stderr, "Maximum number of opening books reached\n");
    }

    fclose(fp);

    if (convert_at_ob_depth != -1) {
        exit(EXIT_SUCCESS);
    }
}

static int get_opening_book_play(play_t * play, uint64_t board_hash) {
    for (unsigned int i = 0; i < opening_book_size; ++i) {
        if (board_hash == opening_book[i] && ob_play_colors[i] == board.color) {
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

// A hash table value is a score in the upper 30 bits and a TYPE_* tag in the lower 2. Scores must
// therefore fit in 30 bits signed; the largest magnitude ever stored is a mate score, ~20000000.
// The shift is done through uint32_t because shifting a negative int left is undefined in C99.
static int pack_score(int score, int type) {
    return (int)(((uint32_t)score << 2) | (uint32_t)type);
}

static int unpack_score(int score_w_type) {
    return score_w_type >> 2;
}

// A play packed into 16 bits: origin square and destination square in 6 bits each and the promotion
// choice in 4. No legal play stays on its own square, so zero is free to mean "no play recorded".
static int16_t pack_play(const play_t * play) {
    int from_p = play->from_y * 8 + play->from_x;
    int to_p = play->to_y * 8 + play->to_x;

    return (int16_t)(from_p | (to_p << 6) | (((int)play->promotion_option) << 12));
}

// Mate scores count plies from the root, so one and the same mate is worth a different amount at
// every depth it is seen from. Entries are stored counting from their own node instead, and shifted
// back to the reading node's depth on the way out, so that an entry written at one ply still names
// the right distance to mate when it is found again at another.
static int score_to_hash(int score, int depth) {
    if (score > MATE_THRESHOLD) {
        return score + depth * 128;
    }
    if (score < -MATE_THRESHOLD) {
        return score - depth * 128;
    }
    return score;
}

static int score_from_hash(int score, int depth) {
    if (score > MATE_THRESHOLD) {
        return score - depth * 128;
    }
    if (score < -MATE_THRESHOLD) {
        return score + depth * 128;
    }
    return score;
}

// The search this table is being used for. Bumped once per move played, so that entries from
// earlier moves stay usable but are the first to be thrown out when room is needed.
static uint8_t hash_table_age = 0;

static void hash_table_reset() {
    if (hash_table != NULL) {
        bzero(hash_table, HASH_TABLE_SIZE * sizeof(hash_table_entry_t));
    }
    hash_table_age = 0;
}

// An entry is filled in if the 2 lowest bits of its score_w_type hold a TYPE_*, i.e. are not zero.
// A hit is marked as belonging to the current search, so that a position still being visited is not
// evicted by the positions around it.
static hash_table_entry_t * hash_table_find(int64_t hash) {
    int start_key = hash & (HASH_TABLE_SIZE - 1);

    for (int i = 0; i < 5; ++i) {
        hash_table_entry_t * entry = &hash_table[(start_key + i) & (HASH_TABLE_SIZE - 1)];

        if ((entry->score_w_type & 3) != 0 && entry->hash == hash) {
            entry->age = hash_table_age;
            return entry;
        }
    }
    return 0;
}

// Which of the five slots a new entry takes: one already holding this position, else an empty one,
// else the least valuable of the five. Value is the draft, since a deep entry stands for far more
// work than a shallow one, less a large penalty for belonging to an earlier search -- an entry the
// current search has not touched is nearly always the one worth losing.
static void hash_table_insert(int64_t hash, int score_w_type, int draft, int16_t best_play) {
    int start_key = hash & (HASH_TABLE_SIZE - 1);

    hash_table_entry_t * victim = 0;
    int victim_value = 0;

    for (int i = 0; i < 5; ++i) {
        hash_table_entry_t * entry = &hash_table[(start_key + i) & (HASH_TABLE_SIZE - 1)];

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

static void populate_zobrist_masks() {
    int nItems = 64 * 12 + 1 + 8 + 4;
    sprintf(buffer, "zobrist_%d.bin", nItems);
    FILE * s = fopen(buffer, "rb");
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

static int64_t hash_from_board(const board_t * board) {
    int64_t hash = 0xAAAAAAAAAAAAAAAA;

    for (int p = 0; p < 64; ++p) {
        char piece = identify_piece(board, p);
        if (piece != ' ') {
            hash = update_hash_with_piece(hash, p, piece);
        }
    }

    if (board->color == BLACK_COLOR) {
        hash ^= zobrist_side_to_move;
    }

    if (board->en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[(int)(board->en_passant_x)];
    }

    if (board->white_right_castling) {
        hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
    }
    if (board->white_left_castling) {
        hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
    }
    if (board->black_right_castling) {
        hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
    }
    if (board->black_left_castling) {
        hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
    }

    return hash;
}

static void fprint_board(FILE * fd, const board_t * board, const board_ext_t * board_ext) {
    board_to_fen(buffer, board, board_ext);
    fprintf(fd, "%s\n", buffer);
    int line_len = 0;

    for (unsigned int i = 0; i < board_ext->past_plays_count; ++i) {
        if ((i & 1) == 0) {
            if (line_len > 68) {
                fprintf(fd, "\n");
                line_len = 0;
            }
            if (line_len == 0) {
                line_len += fprintf(fd, "%d.", i / 2 + 1);
            } else {
                line_len += fprintf(fd, " %d.", i / 2 + 1);
            }
        }
        line_len += fprintf(fd, " %c%d%c%d", 'a' + board_ext->past_plays[i].from_x, 8 - board_ext->past_plays[i].from_y, 'a' + board_ext->past_plays[i].to_x, 8 - board_ext->past_plays[i].to_y);

    }
    if (line_len > 0) {
        fprintf(fd, "\n");
    }

    fprintf(fd, "╔═══╤═══╤═══╤═══╤═══╤═══╤═══╤═══╗┈╮\n");
    for (int y = 0; y < 8; y++) {
        fprintf(fd, "║ ");
        for (int x = 0; x < 8; x++) {
            if (x == board_ext->last_play_x && y == board_ext->last_play_y) {
                fprintf(fd, "\033[32m%c\e[0m", identify_piece(board, y * 8 + x));
            } else {
                fprintf(fd, "%c", identify_piece(board, y * 8 + x));
            }
            if (x < 7) {
                fprintf(fd, " │ ");
            } else {
                fprintf(fd, " ║ %c", 8 - y + '0');
            }
        }
        fprintf(fd, ("\n"));
        if (y < 7) {
            fprintf(fd, "╟───┼───┼───┼───┼───┼───┼───┼───╢ ┊\n");
        }
    }
    fprintf(fd, "╚═══╧═══╧═══╧═══╧═══╧═══╧═══╧═══╝ ┊\n");
    fprintf(fd, "╰┈a┈┈┈b┈┈┈c┈┈┈d┈┈┈e┈┈┈f┈┈┈g┈┈┈h┈┈┈╯\n\n");
}

static void print_board(const board_t * board, const board_ext_t * board_ext) {
    fprint_board(stdout, board, board_ext);
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

    if (board->en_passant_x == to_x) {
        if (from_y == 3 && from_piece == 'P') {
            board->black_pawns ^= (1ULL << (from_y * 8 + to_x));
        }
    }

    if (from_piece == 'K') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->white_rooks ^= (1ULL << (7 * 8 + 5));
                board->white_rooks ^= (1ULL << (7 * 8 + 7));
            } else if (to_x == 2) {
                board->white_rooks ^= (1ULL << (7 * 8 + 0));
                board->white_rooks ^= (1ULL << (7 * 8 + 3));
            }
        }
    }
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

    if (board->en_passant_x == to_x) {
        if (from_y == 4 && from_piece == 'p') {
            board->white_pawns ^= (1ULL << (from_y * 8 + to_x));
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
}

static void just_play_white_pawn(board_t * board, const play_t * play, int64_t * out_hash) {
    int from_x = play->from_x;
    int from_y = play->from_y;
    int to_x = play->to_x;
    int to_y = play->to_y;
    int en_passant_x = board->en_passant_x;
    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;
    char promotion_option = play->promotion_option;

    board->halfmoves = 0;

    char from_piece = 'P';
    char to_piece = identify_piece_black(board, to_p);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_white(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_black(hash, to_p, to_piece);
    }

    hash ^= zobrist_side_to_move;

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'p':
            board->black_pawns ^= to_mask;
            break;
        case 'n':
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= to_mask;
            break;
        case 'k':
            board->black_kings ^= to_mask;
            break;
    }

    board->white_pawns ^= from_mask;

    if (en_passant_x != NO_EN_PASSANT) {
        if (en_passant_x == to_x && from_y == 3) {
            hash = update_hash_with_piece_black(hash, 3 * 8 + en_passant_x, 'p');
            board->black_pawns ^= 1ULL << (3 * 8 + en_passant_x);
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
            board->white_queens ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'Q');
        } else if (promotion_option == PROMOTION_KNIGHT) {
            board->white_knights ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'N');
        } else if (promotion_option == PROMOTION_BISHOP) {
            board->white_bishops ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'B');
        } else {
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
}

static void just_play_white_complex(board_t * board, const play_t * play, int64_t * out_hash) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    int from_p = from_y * 8 + from_x;

    char from_piece = identify_piece_white(board, from_p);
    if (from_piece == 'P') {
        just_play_white_pawn(board, play, out_hash);
        return;
    }

    char to_x = play->to_x;
    char to_y = play->to_y;
    int to_p = to_y * 8 + to_x;

    char to_piece = identify_piece_black(board, to_p);

    board->halfmoves = (to_piece == ' ') ? board->halfmoves + 1 : 0;

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_white(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_black(hash, to_p, to_piece);
    }

    int en_passant_x = board->en_passant_x;
    if (en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'p':
            board->black_pawns ^= to_mask;
            break;
        case 'n':
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= to_mask;
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
            } else if (to_x == 2) {
                board->white_rooks ^= (1ULL << (7 * 8 + 0));
                board->white_rooks ^= (1ULL << (7 * 8 + 3));
                hash = update_hash_with_piece_white(hash, 7 * 8 + 0, 'R');
                hash = update_hash_with_piece_white(hash, 7 * 8 + 3, 'R');
            }
        }

        if (board->white_right_castling) {
            board->white_right_castling = 0;
            hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
        }
        if (board->white_left_castling) {
            board->white_left_castling = 0;
            hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
        }
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

    hash ^= zobrist_side_to_move;

    board->color = BLACK_COLOR;

    *out_hash = hash;
}

static void just_play_black_pawn(board_t * board, const play_t * play, int64_t * out_hash) {
    int from_x = play->from_x;
    int from_y = play->from_y;
    int to_x = play->to_x;
    int to_y = play->to_y;
    int en_passant_x = board->en_passant_x;
    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;
    char promotion_option = play->promotion_option;

    board->halfmoves = 0;

    char from_piece = 'p';
    char to_piece = identify_piece_white(board, to_p);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_black(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_white(hash, to_p, to_piece);
    }

    hash ^= zobrist_side_to_move;

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'P':
            board->white_pawns ^= to_mask;
            break;
        case 'N':
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= to_mask;
            break;
        case 'K':
            board->white_kings ^= to_mask;
            break;
    }

    board->black_pawns ^= from_mask;

    if (en_passant_x != NO_EN_PASSANT) {
        if (en_passant_x == to_x && from_y == 4) {
            hash = update_hash_with_piece_white(hash, 4 * 8 + en_passant_x, 'P');
            board->white_pawns ^= 1ULL << (4 * 8 + en_passant_x);
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
            board->black_queens ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'q');
        } else if (promotion_option == PROMOTION_KNIGHT) {
            board->black_knights ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'n');
        } else if (promotion_option == PROMOTION_BISHOP) {
            board->black_bishops ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'b');
        } else {
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
}

static void just_play_black_complex(board_t * board, const play_t * play, int64_t * out_hash) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    int from_p = from_y * 8 + from_x;

    char from_piece = identify_piece_black(board, from_p);
    if (from_piece == 'p') {
        just_play_black_pawn(board, play, out_hash);
        return;
    }

    char to_x = play->to_x;
    char to_y = play->to_y;
    int to_p = to_y * 8 + to_x;

    char to_piece = identify_piece_white(board, to_p);

    board->halfmoves = (to_piece == ' ') ? board->halfmoves + 1 : 0;

    int64_t hash = *out_hash;

    hash = update_hash_with_piece_black(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece_white(hash, to_p, to_piece);
    }

    int en_passant_x = board->en_passant_x;
    if (en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'P':
            board->white_pawns ^= to_mask;
            break;
        case 'N':
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= to_mask;
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
            } else if (to_x == 2) {
                board->black_rooks ^= (1ULL << (0 * 8 + 0));
                board->black_rooks ^= (1ULL << (0 * 8 + 3));
                hash = update_hash_with_piece_black(hash, 0 * 8 + 0, 'r');
                hash = update_hash_with_piece_black(hash, 0 * 8 + 3, 'r');
            }
        }

        if (board->black_right_castling) {
            board->black_right_castling = 0;
            hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
        }
        if (board->black_left_castling) {
            board->black_left_castling = 0;
            hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
        }
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

    hash ^= zobrist_side_to_move;

    board->color = WHITE_COLOR;

    *out_hash = hash;
}

static void actual_play(board_t * board, board_ext_t * board_ext, const play_t * play) {
    if (board_ext->past_plays_count < 256) {
        board_ext->past_plays[board_ext->past_plays_count].from_x = play->from_x;
        board_ext->past_plays[board_ext->past_plays_count].from_y = play->from_y;
        board_ext->past_plays[board_ext->past_plays_count].to_x = play->to_x;
        board_ext->past_plays[board_ext->past_plays_count].to_y = play->to_y;
        board_ext->past_plays[board_ext->past_plays_count].promotion_option = play->promotion_option;
        board_ext->past_hashes[board_ext->past_plays_count] = hash_from_board(board);
        board_ext->past_plays_count++;
    }

    board_ext->last_play_x = play->to_x;
    board_ext->last_play_y = play->to_y;
    if (board->color == BLACK_COLOR) {
        board_ext->fullmoves++;
    }

    int64_t hash = 0;
    if (board->color == WHITE_COLOR) {
        just_play_white_complex(board, play, &hash);
    } else {
        just_play_black_complex(board, play, &hash);
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

            valid_plays[valid_plays_i].promotion_option = 0;
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

            valid_plays[valid_plays_i].promotion_option = 0;
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

        int to_x = from_x + 1;
        int to = from_y * 8 + from_x;
        uint64_t to_mask = 1ULL << to;
        while (to_x < 8) {
            to_mask = to_mask << 1;
            if (to_mask & white_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = from_y;
            valid_plays_i++;

            if (to_mask & black_mask) {
                break;
            }

            to_x += 1;
        }

        to_x = from_x - 1;
        to = from_y * 8 + from_x;
        to_mask = 1ULL << to;
        while (to_x >= 0) {
            to_mask = to_mask >> 1;
            if (to_mask & white_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = from_y;
            valid_plays_i++;

            if (to_mask & black_mask) {
                break;
            }

            to_x -= 1;
        }

        int to_y = from_y + 1;
        to = from_y * 8 + from_x;
        to_mask = 1ULL << to;
        while (to_y < 8) {
            to_mask = to_mask << 8;
            if (to_mask & white_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = from_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i++;

            if (to_mask & black_mask) {
                break;
            }

            to_y += 1;
        }

        to_y = from_y - 1;
        to = from_y * 8 + from_x;
        to_mask = 1ULL << to;
        while (to_y >= 0) {
            to_mask = to_mask >> 8;
            if (to_mask & white_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = from_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i++;

            if (to_mask & black_mask) {
                break;
            }

            to_y -= 1;
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

                valid_plays[valid_plays_i].promotion_option = 0;
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

        uint64_t moves_to = black_pawn_capture_masks[from] & white_mask;
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

            valid_plays[valid_plays_i].promotion_option = 0;
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

            valid_plays[valid_plays_i].promotion_option = 0;
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

        int to_x = from_x + 1;
        int to = from_y * 8 + from_x;
        uint64_t to_mask = 1ULL << to;
        while (to_x < 8) {
            to_mask = to_mask << 1;
            if (to_mask & black_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = from_y;
            valid_plays_i++;

            if (to_mask & white_mask) {
                break;
            }

            to_x += 1;
        }

        to_x = from_x - 1;
        to = from_y * 8 + from_x;
        to_mask = 1ULL << to;
        while (to_x >= 0) {
            to_mask = to_mask >> 1;
            if (to_mask & black_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = from_y;
            valid_plays_i++;

            if (to_mask & white_mask) {
                break;
            }

            to_x -= 1;
        }

        int to_y = from_y + 1;
        to = from_y * 8 + from_x;
        to_mask = 1ULL << to;
        while (to_y < 8) {
            to_mask = to_mask << 8;
            if (to_mask & black_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = from_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i++;

            if (to_mask & white_mask) {
                break;
            }

            to_y += 1;
        }

        to_y = from_y - 1;
        to = from_y * 8 + from_x;
        to_mask = 1ULL << to;
        while (to_y >= 0) {
            to_mask = to_mask >> 8;
            if (to_mask & black_mask) {
                break;
            }

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = from_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i++;

            if (to_mask & white_mask) {
                break;
            }

            to_y -= 1;
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

                valid_plays[valid_plays_i].promotion_option = 0;
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

// Ignores capturing via en passant
static int square_attacked_by(const board_t * board, int sq, int by_white) {
    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t occupied = white_mask | black_mask;

    uint64_t knights, kings, pawns, pawn_origins, rooks_queens, bishops_queens;
    if (by_white) {
        knights = board->white_knights;
        kings = board->white_kings;
        pawns = board->white_pawns;
        pawn_origins = black_pawn_capture_masks[sq];
        rooks_queens = board->white_rooks | board->white_queens;
        bishops_queens = board->white_bishops | board->white_queens;
    } else {
        knights = board->black_knights;
        kings = board->black_kings;
        pawns = board->black_pawns;
        pawn_origins = white_pawn_capture_masks[sq];
        rooks_queens = board->black_rooks | board->black_queens;
        bishops_queens = board->black_bishops | board->black_queens;
    }

    if (knight_moves_masks[sq] & knights) {
        return 1;
    }
    if (king_moves_masks[sq] & kings) {
        return 1;
    }
    if (pawn_origins & pawns) {
        return 1;
    }

    static const int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
    };

    int from_x = sq % 8;
    int from_y = sq / 8;

    for (int d = 0; d < 8; ++d) {
        uint64_t sliders = d < 4 ? rooks_queens : bishops_queens;
        if (sliders == 0) {
            continue;
        }

        int dx = directions[d][0];
        int dy = directions[d][1];
        int x = from_x + dx;
        int y = from_y + dy;

        while (x >= 0 && x < 8 && y >= 0 && y < 8) {
            uint64_t to_mask = 1ULL << (y * 8 + x);

            if (occupied & to_mask) {
                if (sliders & to_mask) {
                    return 1;
                }
                break;
            }

            x += dx;
            y += dy;
        }
    }

    return 0;
}

static uint64_t compute_pins(const board_t * board, int king_p, int own_is_white, uint64_t * pin_ray) {
    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t occupied = white_mask | black_mask;
    uint64_t own = own_is_white ? white_mask : black_mask;
    uint64_t enemy_rooks_queens = own_is_white ? (board->black_rooks | board->black_queens) : (board->white_rooks | board->white_queens);
    uint64_t enemy_bishops_queens = own_is_white ? (board->black_bishops | board->black_queens) : (board->white_bishops | board->white_queens);

    static const int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
    };

    uint64_t pinned = 0;
    int from_x = king_p % 8;
    int from_y = king_p / 8;

    for (int d = 0; d < 8; ++d) {
        uint64_t sliders = d < 4 ? enemy_rooks_queens : enemy_bishops_queens;
        if (sliders == 0) {
            continue;
        }

        int dx = directions[d][0];
        int dy = directions[d][1];
        int x = from_x + dx;
        int y = from_y + dy;
        uint64_t ray = 0;
        int candidate = -1;

        while (x >= 0 && x < 8 && y >= 0 && y < 8) {
            int to = y * 8 + x;
            uint64_t to_mask = 1ULL << to;
            ray |= to_mask;

            if (occupied & to_mask) {
                if (candidate == -1) {
                    if (!(own & to_mask)) {
                        break;
                    }
                    candidate = to;
                } else {
                    if (sliders & to_mask) {
                        pinned |= 1ULL << candidate;
                        pin_ray[candidate] = ray;
                    }
                    break;
                }
            }

            x += dx;
            y += dy;
        }
    }

    return pinned;
}

static int enumerate_legal_plays_white(play_t * valid_plays, const board_t * board) {
    int valid_plays_i = 0;
    play_t valid_plays_local[218];
    int valid_plays_local_i = enumerate_all_possible_plays_white(valid_plays_local, board);
    board_t board_cpy;

    int king_on_start = (board->white_kings & (1ULL << (7 * 8 + 4))) != 0ULL;
    int king_p = board->white_kings ? __builtin_ctzll(board->white_kings) : -1;

    int in_check = 1;
    uint64_t pinned = 0;
    uint64_t pin_ray[64];

    if (king_p >= 0) {
        in_check = square_attacked_by(board, king_p, 0);
        if (!in_check) {
            pinned = compute_pins(board, king_p, 1, pin_ray);
        }
    }

    // Detect if playing exposes king to immediate capture (illegal move)
    for (int i = 0; i < valid_plays_local_i; ++i) {
        int from_p = valid_plays_local[i].from_y * 8 + valid_plays_local[i].from_x;
        int to_p = valid_plays_local[i].to_y * 8 + valid_plays_local[i].to_x;

        int en_passant = board->en_passant_x == valid_plays_local[i].to_x
            && valid_plays_local[i].from_y == 3
            && (board->white_pawns & (1ULL << from_p)) != 0ULL;

        if (!in_check && from_p != king_p && !en_passant) {
            if ((pinned & (1ULL << from_p)) && !(pin_ray[from_p] & (1ULL << to_p))) {
                continue;
            }
        } else {
            memcpy(&board_cpy, board, sizeof(board_t));
            just_play_white_simple(&board_cpy, &valid_plays_local[i]);

            if (board_cpy.white_kings && square_attacked_by(&board_cpy, __builtin_ctzll(board_cpy.white_kings), 0)) {
                continue;
            }

            int from_x = valid_plays_local[i].from_x;
            int from_y = valid_plays_local[i].from_y;

            // The square the king passes over has to be safe, too
            if (king_on_start && from_x == 4 && from_y == 7) {
                int to_x = valid_plays_local[i].to_x;

                if (to_x == 6) {
                    if (in_check || square_attacked_by(&board_cpy, 7 * 8 + 5, 0)) {
                        continue;
                    }
                } else if (to_x == 2) {
                    if (in_check || square_attacked_by(&board_cpy, 7 * 8 + 3, 0)) {
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
    play_t valid_plays_local[218];
    int valid_plays_local_i = enumerate_all_possible_plays_black(valid_plays_local, board);
    board_t board_cpy;

    int king_on_start = (board->black_kings & (1ULL << (0 * 8 + 4))) != 0ULL;
    int king_p = board->black_kings ? __builtin_ctzll(board->black_kings) : -1;

    int in_check = 1;
    uint64_t pinned = 0;
    uint64_t pin_ray[64];

    if (king_p >= 0) {
        in_check = square_attacked_by(board, king_p, 1);
        if (!in_check) {
            pinned = compute_pins(board, king_p, 0, pin_ray);
        }
    }

    // Detect if playing exposes king to immediate capture (illegal move)
    for (int i = 0; i < valid_plays_local_i; ++i) {
        int from_p = valid_plays_local[i].from_y * 8 + valid_plays_local[i].from_x;
        int to_p = valid_plays_local[i].to_y * 8 + valid_plays_local[i].to_x;

        int en_passant = board->en_passant_x == valid_plays_local[i].to_x
            && valid_plays_local[i].from_y == 4
            && (board->black_pawns & (1ULL << from_p)) != 0ULL;

        if (!in_check && from_p != king_p && !en_passant) {
            if ((pinned & (1ULL << from_p)) && !(pin_ray[from_p] & (1ULL << to_p))) {
                continue;
            }
        } else {
            memcpy(&board_cpy, board, sizeof(board_t));
            just_play_black_simple(&board_cpy, &valid_plays_local[i]);

            if (board_cpy.black_kings && square_attacked_by(&board_cpy, __builtin_ctzll(board_cpy.black_kings), 1)) {
                continue;
            }

            int from_x = valid_plays_local[i].from_x;
            int from_y = valid_plays_local[i].from_y;

            // The square the king passes over has to be safe, too
            if (king_on_start && from_x == 4 && from_y == 0) {
                int to_x = valid_plays_local[i].to_x;

                if (to_x == 6) {
                    if (in_check || square_attacked_by(&board_cpy, 0 * 8 + 5, 1)) {
                        continue;
                    }
                } else if (to_x == 2) {
                    if (in_check || square_attacked_by(&board_cpy, 0 * 8 + 3, 1)) {
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

static int king_threatened_white(const board_t * board) {
    return board->white_kings != 0ULL && square_attacked_by(board, __builtin_ctzll(board->white_kings), 0);
}

static int king_threatened_black(const board_t * board) {
    return board->black_kings != 0ULL && square_attacked_by(board, __builtin_ctzll(board->black_kings), 1);
}

static int king_threatened(const board_t * board) {
    if (board->color == WHITE_COLOR) {
        return king_threatened_white(board);
    } else {
        return king_threatened_black(board);
    }
}

static int estimate_board_score(const board_t * board) {
    int score = 0;

    const int knight_pst[64] = {
      -50,-40,-30,-30,-30,-30,-40,-50,
      -40,-20,  0,  0,  0,  0,-20,-40,
      -30,  0, 10, 15, 15, 10,  0,-30,
      -30,  5, 15, 20, 20, 15,  5,-30,
      -30,  0, 15, 20, 20, 15,  0,-30,
      -30,  5, 10, 15, 15, 10,  5,-30,
      -40,-20,  0,  5,  5,  0,-20,-40,
      -50,-40,-30,-30,-30,-30,-40,-50
    };

    uint64_t knights = board->white_knights;
    while (knights) {
        int sq = __builtin_ctzll(knights);
        score += 320 + knight_pst[sq];
        knights &= knights - 1;
    }

    knights = board->black_knights;
    while (knights) {
        int sq = __builtin_ctzll(knights);
        score -= 320 + knight_pst[sq ^ 56];
        knights &= knights - 1;
    }

    const int bishop_pst[64] = {
      -20,-10,-10,-10,-10,-10,-10,-20,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5, 10, 10,  5,  0,-10,
      -10,  5,  5, 10, 10,  5,  5,-10,
      -10,  0, 10, 10, 10, 10,  0,-10,
      -10,  5,  5, 10, 10,  5,  5,-10,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -20,-10,-10,-10,-10,-10,-10,-20
    };

    uint64_t bishops = board->white_bishops;
    while (bishops) {
        int sq = __builtin_ctzll(bishops);
        score += 330 + bishop_pst[sq];
        bishops &= bishops - 1;
    }

    bishops = board->black_bishops;
    while (bishops) {
        int sq = __builtin_ctzll(bishops);
        score -= 330 + bishop_pst[sq ^ 56];
        bishops &= bishops - 1;
    }

    const int rook_pst[64] = {
       0,  0,  0,  0,  0,  0,  0,  0,
       5, 10, 10, 10, 10, 10, 10,  5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
       0,  0,  0,  5,  5,  0,  0,  0
    };

    uint64_t rooks = board->white_rooks;
    while (rooks) {
        int sq = __builtin_ctzll(rooks);
        score += 500 + rook_pst[sq];
        rooks &= rooks - 1;
    }

    rooks = board->black_rooks;
    while (rooks) {
        int sq = __builtin_ctzll(rooks);
        score -= 500 + rook_pst[sq ^ 56];
        rooks &= rooks - 1;
    }

    const int queen_pst[64] = {
      -20,-10,-10, -5, -5,-10,-10,-20,
      -10,  0,  5,  0,  0,  0,  0,-10,
      -10,  0,  5,  5,  5,  5,  0,-10,
       -5,  0,  5,  5,  5,  5,  0, -5,
        0,  0,  5,  5,  5,  5,  0, -5,
      -10,  0,  5,  5,  5,  5,  0,-10,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -20,-10,-10, -5, -5,-10,-10,-20
    };

    uint64_t queens = board->white_queens;
    while (queens) {
        int sq = __builtin_ctzll(queens);
        score += 900 + queen_pst[sq];
        queens &= queens - 1;
    }

    queens = board->black_queens;
    while (queens) {
        int sq = __builtin_ctzll(queens);
        score -= 900 + queen_pst[sq ^ 56];
        queens &= queens - 1;
    }

/*
    const int king_midgame_pst[64] = {
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
         20,  20,   0,   0,   0,   0,  20,  20,
         20,  30,  10,   0,   0,  10,  30,  20,
    };

    const int king_endgame_pst[64] = {
        -50, -40, -30, -20, -20, -30, -40, -50,
        -30, -20, -10,   0,   0, -10, -20, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -30,   0,   0,   0,   0, -30, -30,
        -50, -30, -30, -30, -30, -30, -30, -50,
    };

    const int * king_pst = score > 1450 ? king_midgame_pst : king_endgame_pst;

    uint64_t kings = board->white_kings;
    if (kings) {
        int sq = __builtin_ctzll(kings);
        score += king_pst[sq];
    }

    kings = board->black_kings;
    if (kings) {
        int sq = __builtin_ctzll(kings);
        score -= king_pst[sq ^ 56];
    }
*/

    const int pawn_pst[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
         5,  5, 10, 25, 25, 10,  5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5, -5,-10,  0,  0,-10, -5,  5,
         5, 10, 10,-20,-20, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0
    };

    uint64_t pawns = board->white_pawns;
    while (pawns) {
        int sq = __builtin_ctzll(pawns);
        score += 100 + pawn_pst[sq];
        pawns &= pawns - 1;
    }

    pawns = board->black_pawns;
    while (pawns) {
        int sq = __builtin_ctzll(pawns);
        score -= 100 + pawn_pst[sq ^ 56];
        pawns &= pawns - 1;
    }

    return score;
}

static int minimax_black_capture_only(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash);

// Quiescence search. Reached from the frontier of the main search, it keeps resolving captures until
// the position is quiet, so that estimate_board_score is never taken in the middle of a trade.
//
// The side to move may always stand pat, i.e. stop capturing and accept the static score, since it
// is under no obligation to enter a capture sequence; that score is the floor (for white) or the
// ceiling (for black) of the node. The one exception is being in check, where doing nothing is not
// legal: there every reply is searched, quiet ones included, and only the depth limit ends it.
static int minimax_white_capture_only(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash) {
    if (search_aborted || out_of_time()) {
        search_aborted = 1;
        return 0;
    }

    search_history[search_history_count + depth] = hash;

    if (is_repetition(hash, depth, board->halfmoves) || board->halfmoves >= 100 || insufficient_material(board)) {
        return DRAW_SCORE;
    }

    // Quiescence nodes look at captures only, so whatever they return is worth no full ply of
    // search and is stored at a draft of 0
    int draft = 0;
    int16_t tt_play = 0;

    hash_table_entry_t * entry = hash_table_find(hash);
    if (entry != 0) {
        tt_play = entry->best_play;

        // Only a search that still had at least as far to go as this one says anything about this
        // node; a shallower entry keeps its play
        if (entry->draft >= draft) {
            int score = score_from_hash(unpack_score(entry->score_w_type), depth);
            int type = entry->score_w_type & 3;

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
    }

    int alpha_orig = alpha;
    int beta_orig = beta;

    if (depth == max_depth) {
        int score = estimate_board_score(board);

        hash_table_insert(hash, pack_score(score, TYPE_EXACT), 0, 0);
        return score;
    }

    board_t board_cpy;
    // For why 218, see https://lichess.org/@/Tobs40/blog/why-a-position-cant-have-more-than-218-moves/a5xdxeqs
    play_t valid_plays[218];

    int valid_plays_i = enumerate_legal_plays_white(valid_plays, board);
    int in_check = king_threatened_white(board);

    if (valid_plays_i == 0) {
        int score = in_check ? -MATE_SCORE + depth * 128 : DRAW_SCORE;

        // A finished game is worth the same however many plies were left to search, so this is
        // the one result that can be stored at the greatest draft there is.
        hash_table_insert(hash, pack_score(score_to_hash(score, depth), TYPE_EXACT), MAX_TOTAL_SEARCH_DEPTH, 0);
        return score;
    }

    // The play that came out best the last time this position was searched goes first. If it is a
    // quiet play and this node is not in check, the capture filter in the loop below drops it again.
    if (tt_play != 0) {
        for (int i = 0; i < valid_plays_i; ++i) {
            if (pack_play(&valid_plays[i]) == tt_play) {
                play_t play_tmp = valid_plays[0];
                valid_plays[0] = valid_plays[i];
                valid_plays[i] = play_tmp;
                break;
            }
        }
    }

    int best_score;
    int16_t best_play = 0;

    if (in_check) {
        best_score = NO_SCORE;
    } else {
        best_score = estimate_board_score(board);

        if (best_score >= beta) {
            hash_table_insert(hash, pack_score(best_score, TYPE_LOWER_BOUND), 0, 0);
            return best_score;
        }

        alpha = MAX(alpha, best_score);
    }

    for (int i = 0; i < valid_plays_i; ++i) {
        if (!in_check && identify_piece_black(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x) == ' ') {
            continue;
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        just_play_white_complex(&board_cpy, &valid_plays[i], &this_hash);

        int score = minimax_black_capture_only(&board_cpy, depth + 1, max_depth, alpha, beta, this_hash);

        if (score != NO_SCORE) {
            if (best_score == NO_SCORE || score > best_score) {
                best_score = score;
                best_play = pack_play(&valid_plays[i]);
            }
            if (best_score >= beta) {
                break;
            }
            alpha = MAX(alpha, best_score);
        }
    }

    if (!search_aborted && best_score != NO_SCORE) {
        int type;
        if (best_score <= alpha_orig) {
            type = TYPE_UPPER_BOUND;
        } else if (best_score >= beta_orig) {
            type = TYPE_LOWER_BOUND;
        } else {
            type = TYPE_EXACT;
        }

        hash_table_insert(hash, pack_score(score_to_hash(best_score, depth), type), draft, best_play);
    }

    return best_score;
}

static int minimax_black_capture_only(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash) {
    if (search_aborted || out_of_time()) {
        search_aborted = 1;
        return 0;
    }

    search_history[search_history_count + depth] = hash;

    if (is_repetition(hash, depth, board->halfmoves) || board->halfmoves >= 100 || insufficient_material(board)) {
        return DRAW_SCORE;
    }

    // Quiescence nodes look at captures only, so whatever they return is worth no full ply of
    // search and is stored at a draft of 0
    int draft = 0;
    int16_t tt_play = 0;

    hash_table_entry_t * entry = hash_table_find(hash);
    if (entry != 0) {
        tt_play = entry->best_play;

        // Only a search that still had at least as far to go as this one says anything about this
        // node; a shallower entry keeps its play
        if (entry->draft >= draft) {
            int score = score_from_hash(unpack_score(entry->score_w_type), depth);
            int type = entry->score_w_type & 3;

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
    }

    int alpha_orig = alpha;
    int beta_orig = beta;

    if (depth == max_depth) {
        int score = estimate_board_score(board);

        hash_table_insert(hash, pack_score(score, TYPE_EXACT), 0, 0);
        return score;
    }

    board_t board_cpy;
    play_t valid_plays[218];

    int valid_plays_i = enumerate_legal_plays_black(valid_plays, board);
    int in_check = king_threatened_black(board);

    if (valid_plays_i == 0) {
        int score = in_check ? MATE_SCORE - depth * 128 : DRAW_SCORE;

        // A finished game is worth the same however many plies were left to search, so this is
        // the one result that can be stored at the greatest draft there is.
        hash_table_insert(hash, pack_score(score_to_hash(score, depth), TYPE_EXACT), MAX_TOTAL_SEARCH_DEPTH, 0);
        return score;
    }

    // The play that came out best the last time this position was searched goes first. If it is a
    // quiet play and this node is not in check, the capture filter in the loop below drops it again.
    if (tt_play != 0) {
        for (int i = 0; i < valid_plays_i; ++i) {
            if (pack_play(&valid_plays[i]) == tt_play) {
                play_t play_tmp = valid_plays[0];
                valid_plays[0] = valid_plays[i];
                valid_plays[i] = play_tmp;
                break;
            }
        }
    }

    int best_score;
    int16_t best_play = 0;

    if (in_check) {
        best_score = NO_SCORE;
    } else {
        best_score = estimate_board_score(board);

        if (best_score <= alpha) {
            hash_table_insert(hash, pack_score(best_score, TYPE_UPPER_BOUND), 0, 0);
            return best_score;
        }

        beta = MIN(beta, best_score);
    }

    for (int i = 0; i < valid_plays_i; ++i) {
        if (!in_check && identify_piece_white(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x) == ' ') {
            continue;
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        just_play_black_complex(&board_cpy, &valid_plays[i], &this_hash);

        int score = minimax_white_capture_only(&board_cpy, depth + 1, max_depth, alpha, beta, this_hash);

        if (score != NO_SCORE) {
            if (best_score == NO_SCORE || score < best_score) {
                best_score = score;
                best_play = pack_play(&valid_plays[i]);
            }
            if (best_score <= alpha) {
                break;
            }
            beta = MIN(beta, best_score);
        }
    }

    if (!search_aborted && best_score != NO_SCORE) {
        int type;
        if (best_score <= alpha_orig) {
            type = TYPE_UPPER_BOUND;
        } else if (best_score >= beta_orig) {
            type = TYPE_LOWER_BOUND;
        } else {
            type = TYPE_EXACT;
        }

        hash_table_insert(hash, pack_score(score_to_hash(best_score, depth), type), draft, best_play);
    }

    return best_score;
}

static int minimax_black(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash);

static int minimax_white(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash) {
    if (search_aborted || out_of_time()) {
        search_aborted = 1;
        return 0;
    }

    search_history[search_history_count + depth] = hash;

    if (is_repetition(hash, depth, board->halfmoves) || board->halfmoves >= 100 || insufficient_material(board)) {
        return DRAW_SCORE;
    }

    // How many plies this node still has to search below it, which is what its score is worth.
    int draft = max_depth - depth;
    int16_t tt_play = 0;

    hash_table_entry_t * entry = hash_table_find(hash);
    if (entry != 0) {
        tt_play = entry->best_play;

        // Only a search that still had at least as far to go as this one says anything about this
        // node; a shallower entry keeps its play
        if (entry->draft >= draft) {
            int score = score_from_hash(unpack_score(entry->score_w_type), depth);
            int type = entry->score_w_type & 3;

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
    }

    int alpha_orig = alpha;
    int beta_orig = beta;

    if (depth == max_depth) {
        int score = estimate_board_score(board);

        hash_table_insert(hash, pack_score(score, TYPE_EXACT), 0, 0);
        return score;
    }

    board_t board_cpy;
    // For why 218, see https://lichess.org/@/Tobs40/blog/why-a-position-cant-have-more-than-218-moves/a5xdxeqs
    play_t valid_plays[218];
    char captures[218];

    int valid_plays_i = enumerate_legal_plays_white(valid_plays, board);
    if (valid_plays_i == 0) {
        int score = king_threatened_white(board) ? -MATE_SCORE + depth * 128 : DRAW_SCORE;

        // A finished game is worth the same however many plies were left to search, so this is
        // the one result that can be stored at the greatest draft there is.
        hash_table_insert(hash, pack_score(score_to_hash(score, depth), TYPE_EXACT), MAX_TOTAL_SEARCH_DEPTH, 0);
        return score;
    }

    int best_score = NO_SCORE;
    int16_t best_play = 0;
    int breakfor = 0;

    // The play that came out best the last time this position was searched, however shallowly, is
    // the best guess there is and costs nothing to make: it goes ahead of even the captures,
    // because a first play good enough to cut cancels the whole rest of the list.
    int tt_play_first = 0;
    if (tt_play != 0) {
        for (int i = 0; i < valid_plays_i; ++i) {
            if (pack_play(&valid_plays[i]) == tt_play) {
                play_t play_tmp = valid_plays[0];
                valid_plays[0] = valid_plays[i];
                valid_plays[i] = play_tmp;
                tt_play_first = 1;
                break;
            }
        }
    }

    // captures[] is filled in as the loop reaches each play and not before, so that a cut on an
    // early one leaves the rest of the list untouched.
    for (int i = 0; i < valid_plays_i; ++i) {
        captures[i] = identify_piece_black(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x) != ' ';
        if (i == 0 && tt_play_first) {
            // Taken here whether it is a capture or not, and marked so the quiet pass skips it.
            captures[0] = 1;
        } else if (!captures[i]) {
            continue;
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        just_play_white_complex(&board_cpy, &valid_plays[i], &this_hash);

        int score;
        if (depth + 1 == max_depth) {
            score = minimax_black_capture_only(&board_cpy, depth + 1, max_depth + QUIESCENCE_EXTRA_DEPTH, alpha, beta, this_hash);
        } else {
            score = minimax_black(&board_cpy, depth + 1, max_depth, alpha, beta, this_hash);
        }

        if (score != NO_SCORE) {
            if (best_score == NO_SCORE || score > best_score) {
                best_score = score;
                best_play = pack_play(&valid_plays[i]);
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

            just_play_white_complex(&board_cpy, &valid_plays[i], &this_hash);

            // A quiet move at the frontier hands off to the quiescence search exactly like a
            // capture does: it can just as easily leave a piece hanging, and taking the static
            // score there instead is what let the search walk into losing one.
            int score;
            if (depth + 1 == max_depth) {
                score = minimax_black_capture_only(&board_cpy, depth + 1, max_depth + QUIESCENCE_EXTRA_DEPTH, alpha, beta, this_hash);
            } else {
                score = minimax_black(&board_cpy, depth + 1, max_depth, alpha, beta, this_hash);
            }

            if (score != NO_SCORE) {
                if (best_score == NO_SCORE || score > best_score) {
                    best_score = score;
                    best_play = pack_play(&valid_plays[i]);
                }
                if (score >= beta) {
                    break;
                }
                alpha = MAX(alpha, score);
            }
        }
    }

    if (!search_aborted && best_score != NO_SCORE) {
        int type;
        if (best_score <= alpha_orig) {
            type = TYPE_UPPER_BOUND;
        } else if (best_score >= beta_orig) {
            type = TYPE_LOWER_BOUND;
        } else {
            type = TYPE_EXACT;
        }

        hash_table_insert(hash, pack_score(score_to_hash(best_score, depth), type), draft, best_play);
    }

    return best_score;
}

static int minimax_black(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash) {
    if (search_aborted || out_of_time()) {
        search_aborted = 1;
        return 0;
    }

    search_history[search_history_count + depth] = hash;

    if (is_repetition(hash, depth, board->halfmoves) || board->halfmoves >= 100 || insufficient_material(board)) {
        return DRAW_SCORE;
    }

    // How many plies this node still has to search below it, which is what its score is worth.
    int draft = max_depth - depth;
    int16_t tt_play = 0;

    hash_table_entry_t * entry = hash_table_find(hash);
    if (entry != 0) {
        tt_play = entry->best_play;

        // Only a search that still had at least as far to go as this one says anything about this
        // node; a shallower entry keeps its play
        if (entry->draft >= draft) {
            int score = score_from_hash(unpack_score(entry->score_w_type), depth);
            int type = entry->score_w_type & 3;

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
    }

    int alpha_orig = alpha;
    int beta_orig = beta;

    if (depth == max_depth) {
        int score = estimate_board_score(board);

        hash_table_insert(hash, pack_score(score, TYPE_EXACT), 0, 0);
        return score;
    }

    board_t board_cpy;
    play_t valid_plays[218];
    char captures[218];

    int valid_plays_i = enumerate_legal_plays_black(valid_plays, board);
    if (valid_plays_i == 0) {
        int score = king_threatened_black(board) ? MATE_SCORE - depth * 128 : DRAW_SCORE;

        // A finished game is worth the same however many plies were left to search, so this is
        // the one result that can be stored at the greatest draft there is.
        hash_table_insert(hash, pack_score(score_to_hash(score, depth), TYPE_EXACT), MAX_TOTAL_SEARCH_DEPTH, 0);
        return score;
    }

    int best_score = NO_SCORE;
    int16_t best_play = 0;
    int breakfor = 0;

    // The play that came out best the last time this position was searched, however shallowly, is
    // the best guess there is and costs nothing to make: it goes ahead of even the captures,
    // because a first play good enough to cut cancels the whole rest of the list.
    int tt_play_first = 0;
    if (tt_play != 0) {
        for (int i = 0; i < valid_plays_i; ++i) {
            if (pack_play(&valid_plays[i]) == tt_play) {
                play_t play_tmp = valid_plays[0];
                valid_plays[0] = valid_plays[i];
                valid_plays[i] = play_tmp;
                tt_play_first = 1;
                break;
            }
        }
    }

    // captures[] is filled in as the loop reaches each play and not before, so that a cut on an
    // early one leaves the rest of the list untouched.
    for (int i = 0; i < valid_plays_i; ++i) {
        captures[i] = identify_piece_white(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x) != ' ';
        if (i == 0 && tt_play_first) {
            // Taken here whether it is a capture or not, and marked so the quiet pass skips it.
            captures[0] = 1;
        } else if (!captures[i]) {
            continue;
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        just_play_black_complex(&board_cpy, &valid_plays[i], &this_hash);

        int score;
        if (depth + 1 == max_depth) {
            score = minimax_white_capture_only(&board_cpy, depth + 1, max_depth + QUIESCENCE_EXTRA_DEPTH, alpha, beta, this_hash);
        } else {
            score = minimax_white(&board_cpy, depth + 1, max_depth, alpha, beta, this_hash);
        }

        if (score != NO_SCORE) {
            if (best_score == NO_SCORE || score < best_score) {
                best_score = score;
                best_play = pack_play(&valid_plays[i]);
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

            just_play_black_complex(&board_cpy, &valid_plays[i], &this_hash);

            // A quiet move at the frontier hands off to the quiescence search exactly like a
            // capture does: it can just as easily leave a piece hanging, and taking the static
            // score there instead is what let the search walk into losing one.
            int score;
            if (depth + 1 == max_depth) {
                score = minimax_white_capture_only(&board_cpy, depth + 1, max_depth + QUIESCENCE_EXTRA_DEPTH, alpha, beta, this_hash);
            } else {
                score = minimax_white(&board_cpy, depth + 1, max_depth, alpha, beta, this_hash);
            }

            if (score != NO_SCORE) {
                if (best_score == NO_SCORE || score < best_score) {
                    best_score = score;
                    best_play = pack_play(&valid_plays[i]);
                }
                if (score <= alpha) {
                    break;
                }
                beta = MIN(beta, score);
            }
        }
    }

    if (!search_aborted && best_score != NO_SCORE) {
        int type;
        if (best_score <= alpha_orig) {
            type = TYPE_UPPER_BOUND;
        } else if (best_score >= beta_orig) {
            type = TYPE_LOWER_BOUND;
        } else {
            type = TYPE_EXACT;
        }

        hash_table_insert(hash, pack_score(score_to_hash(best_score, depth), type), draft, best_play);
    }

    return best_score;
}

// True only for dead positions, i.e. where no sequence of legal moves by either side can produce a
// checkmate. That is K vs K, K plus a single minor vs K, and K+B vs K+B with both bishops on squares
// of the same color. Everything else, K+N+N vs K included, is still playable: a mate cannot be
// forced there but it can be reached, so the search is left to work it out rather than being told
// the game is over.
static int insufficient_material(const board_t * board) {
    if (board->white_queens || board->black_queens || board->white_rooks || board->black_rooks || board->white_pawns || board->black_pawns) {
        return 0;
    }

    int white_knights = __builtin_popcountll(board->white_knights);
    int black_knights = __builtin_popcountll(board->black_knights);
    int white_bishops = __builtin_popcountll(board->white_bishops);
    int black_bishops = __builtin_popcountll(board->black_bishops);

    int white_bns = white_knights + white_bishops;
    int black_bns = black_knights + black_bishops;

    // K vs K, and K plus one minor vs K
    if (white_bns + black_bns <= 1) {
        return 1;
    }

    // K+B vs K+B, drawn when both bishops travel on the same color squares
    if (white_bishops == 1 && black_bishops == 1 && white_bns == 1 && black_bns == 1) {
        int white_sq = __builtin_ctzll(board->white_bishops);
        int black_sq = __builtin_ctzll(board->black_bishops);
        int white_color = ((white_sq >> 3) + (white_sq & 7)) & 1;
        int black_color = ((black_sq >> 3) + (black_sq & 7)) & 1;
        return white_color == black_color;
    }

    return 0;
}

static int is_game_drawn() {
    if (insufficient_material(&board) || board.halfmoves >= 100) {
        return 1;
    }

    // Threefold repetition, the position on the board being the third occurrence.
    int64_t hash = hash_from_board(&board);
    int seen = 0;

    for (unsigned int i = 0; i < board_ext.past_plays_count; ++i) {
        if (board_ext.past_hashes[i] == hash) {
            seen++;
        }
    }

    return seen >= 2;
}

static int ai_play(play_t * play) {
    if (opening_book_enabled) {
        uint64_t board_hash = hash_from_board(&board);
        int play_found = get_opening_book_play(play, board_hash);
        if (play_found) {
            actual_play(&board, &board_ext, play);
            return 1;
        }
    }
    if (arbitrate_draws && is_game_drawn()) {
        return DRAW;
    }

    play_t valid_plays[218];
    board_t board_cpy;

    int valid_plays_i = enumerate_legal_plays(valid_plays, &board);
    if (valid_plays_i == 0) {
        if (king_threatened(&board)) {
            return CHECK_MATE;
        } else {
            return DRAW;
        }
    }

    ++hash_table_age;

    int64_t root_hash = hash_from_board(&board);

    search_history_count = 0;
    for (unsigned int i = 0; i < board_ext.past_plays_count; ++i) {
        search_history[search_history_count++] = board_ext.past_hashes[i];
    }
    search_history[search_history_count++] = root_hash;

    gettimeofday(&search_start, NULL);
    search_aborted = 0;
    search_nodes = 0;

    int best_score = NO_SCORE;

    for (int max_depth = 1; max_depth <= search_depth_limit; ++max_depth) {
        search_abortable = max_depth > 1;

        int alpha = -2147483644;
        int beta = 2147483644;
        int iter_score = NO_SCORE;
        int iter_play = 0;

        for (int i = 0; i < valid_plays_i; ++i) {
            memcpy(&board_cpy, &board, sizeof(board_t));

            int64_t child_hash = root_hash;

            if (board_cpy.color == WHITE_COLOR) {
                just_play_white_complex(&board_cpy, &valid_plays[i], &child_hash);
            } else {
                just_play_black_complex(&board_cpy, &valid_plays[i], &child_hash);
            }

            int score;
            if (board_cpy.color == WHITE_COLOR) {
                score = minimax_white(&board_cpy, 0, max_depth, alpha, beta, child_hash);
            } else {
                score = minimax_black(&board_cpy, 0, max_depth, alpha, beta, child_hash);
            }

            if (search_aborted) {
                break;
            }

            if (score != NO_SCORE) {
                if (board.color == WHITE_COLOR) {
                    if (iter_score == NO_SCORE || score > iter_score) {
                        iter_score = score;
                        iter_play = i;
                    }
                    alpha = MAX(alpha, score);
                } else {
                    if (iter_score == NO_SCORE || score < iter_score) {
                        iter_score = score;
                        iter_play = i;
                    }
                    beta = MIN(beta, score);
                }
            }
        }

        // An iteration that ran out of time part way through has only seen some of the plays, so
        // the one it likes best means nothing. The previous iteration's answer stands.
        if (search_aborted || iter_score == NO_SCORE) {
            break;
        }

        best_score = iter_score;

        // We lead the next iteration with this one's best play for the most gain of searching by
        // increasing depth comes from.
        if (iter_play != 0) {
            play_t tmp_play = valid_plays[0];
            valid_plays[0] = valid_plays[iter_play];
            valid_plays[iter_play] = tmp_play;
        }

        if (best_score >= MATE_THRESHOLD || best_score <= -MATE_THRESHOLD) {
            break;
        }

        // Starting an iteration there is no chance of finishing spends the rest of the budget on a
        // result that gets thrown away.
        if (search_budget_ms != 0 && search_elapsed_ms() * 3 >= search_budget_ms) {
            break;
        }
    }

    // The first iteration is never abandoned part way, so valid_plays[0] is always the best
    // play of the deepest iteration that finished.
    actual_play(&board, &board_ext, &valid_plays[0]);
    *play = valid_plays[0];
    return 1;
}

// Past the end of standard input fgets returns immediately and forever, which turns every prompt
// below into a busy loop, so a closed input ends the program instead.
static void read_input_line() {
    if (fgets(buffer, 1024, stdin) == NULL) {
        printf("\n");
        exit(EXIT_SUCCESS);
    }
}

static char input_promotion_piece() {
    while (1) {
        printf("Promotion choice (options: Q, N, B, R): ");
        read_input_line();

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
    play->promotion_option = 0;
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

static void undo_last_plays() {
    unsigned past_plays = board_ext.past_plays_count - 2;
    reset_board();
    for (unsigned int i = 0; i < past_plays; ++i) {
        actual_play(&board, &board_ext, &board_ext.past_plays[i]);
    }
    board_ext.past_plays_count = past_plays;
}

static int input_play(play_t * play, const play_t * valid_plays, int valid_plays_i) {
    while (1) {
        printf("Input (example: e2e4): ");
        read_input_line();

        buffer[5] = 0;
        if (strcmp(buffer, "quit\n") == 0) {
            exit(EXIT_SUCCESS);
        }
        if (strcmp(buffer, "undo\n") == 0) {
            if (board_ext.past_plays_count >= 2 && board_ext.past_plays_count < 256) {
                undo_last_plays();
                return 0;
            } else {
                printf("Undo is not possible from this position.\n");
            }
        }
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
                if (play->promotion_option != 0) {
                    char from_piece = identify_piece(&board, play->from_y * 8 + play->from_x);
                    play->promotion_option = 0;
                    if (play->from_y == 1 && play->to_y == 0 && from_piece == 'P') {
                        play->promotion_option = input_promotion_piece();
                    } else if (play->from_y == 6 && play->to_y == 7 && from_piece == 'p') {
                        play->promotion_option = input_promotion_piece();
                    }
                }

                printf("\n");
                return 1;
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
    board_ext.fullmoves = 0;
    board_ext.past_plays_count = 0;
    board_ext.last_play_x = -1;
}

static void send_uci_command(FILE * fd, const char * str) {
    fprintf(fd, "< %s\n", str);
    fflush(fd);
    printf("%s\n", str);
    fflush(stdout);
}

static void text_mode_play(int player_one_is_human, int player_two_is_human) {
    play_t valid_plays[218];

    while (1) {
        print_board(&board, &board_ext);
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
            int played = input_play(&play, valid_plays, valid_plays_i);
            if (played) {
                actual_play(&board, &board_ext, &play);
            }
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
        read_input_line();

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

static FILE * init_log_file(const char * program_name) {
    const char * charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    char str[5];
    for (int i = 0; i < 4; i++) {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[4] = 0;

    sprintf(buffer, "%s_%ld_%s.log", program_name, time(NULL), str);

    FILE * fd = fopen(buffer, "a");
    if (fd == NULL) {
        fprintf(stderr, "Could not open log file %s, logging to stderr.\n", buffer);
        return stderr;
    }

    return fd;
}

// Value of a "name N" pair in a go command, or -1 when the command does not carry it.
static long read_go_option(const char * cmd, const char * name) {
    char key[32];
    sprintf(key, " %s ", name);

    const char * at = strstr(cmd, key);
    if (at == NULL) {
        return -1;
    }

    return atol(at + strlen(key));
}

static void set_search_limits(const char * cmd) {
    search_depth_limit = DEFAULT_SEARCH_DEPTH;
    search_budget_ms = 0;

    long depth = read_go_option(cmd, "depth");
    long movetime = read_go_option(cmd, "movetime");
    long my_time = read_go_option(cmd, board.color == WHITE_COLOR ? "wtime" : "btime");
    long my_increment = read_go_option(cmd, board.color == WHITE_COLOR ? "winc" : "binc");
    long movestogo = read_go_option(cmd, "movestogo");

    // "go infinite" is deliberately not honoured: without a "stop" command to end it
    if (depth > 0) {
        search_depth_limit = (int)MIN(depth, (long)MAX_SEARCH_DEPTH);
    } else if (movetime > 0) {
        search_depth_limit = MAX_SEARCH_DEPTH;
        search_budget_ms = movetime;
    } else if (my_time > 0) {
        // With no movestogo the game is assumed to have about 30 moves left in it, which is what
        // keeps the early moves from eating a clock the endgame still needs.
        search_depth_limit = MAX_SEARCH_DEPTH;
        search_budget_ms = my_time / (movestogo > 0 ? movestogo : 30) + (my_increment > 0 ? my_increment * 3 / 4 : 0);
    }

    if (search_budget_ms != 0 && my_time > 0 && search_budget_ms > my_time - 50) {
        search_budget_ms = MAX(my_time - 50, 10);
    }
}

static void uci_mode(FILE * fd) {
    fprintf(fd, "# Starting in UCI mode.\n");
    fflush(fd);

    arbitrate_draws = extend_uci;

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

        if (strcmp(buffer, "quit") == 0) {
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
            // The only point at which what the table holds stops being about this game. A
            // "position" command is not one: it names a position in the same game, and the entries
            // from the searches that led to it are exactly the ones worth keeping.
            hash_table_reset();
            uci_game_in_error_state = 0;
            continue;
        }
        if (strncmp(buffer, "position ", strlen("position ")) == 0) {
            char * str = strstr(buffer, " startpos ");
            if (str) {
                reset_board();
                uci_game_in_error_state = 0;
            } else {
                str = strstr(buffer, " fen ");
                if (str) {
                    fen_to_board(&board, &board_ext, str + strlen(" fen "));
                    uci_game_in_error_state = 0;
                }
            }

            str = strstr(buffer, " moves ");
            if (str) {
                str += strlen(" moves ");

                while (str) {
                    play_t play;
                    str = read_play(&play, str);
                    if (str) {
                        play_t valid_plays[218];
                        int valid_plays_i = enumerate_legal_plays(valid_plays, &board);
                        if (valid_plays_i == 0) {
                            fprint_board(stderr, &board, &board_ext);

                            fprintf(fd, "# Invalid play detected - game is over\n");
                            fflush(fd);
                            if (extend_uci) {
                                uci_game_in_error_state = 1;
                            }
                            break;
                        }

                        int play_is_valid = 0;
                        for (int i = 0; i < valid_plays_i; ++i) {
                            if (play.from_x == valid_plays[i].from_x && play.from_y == valid_plays[i].from_y && play.to_x == valid_plays[i].to_x && play.to_y == valid_plays[i].to_y) {
                                play_is_valid = 1;
                                break;
                            }
                        }
                        if (play_is_valid) {
                            actual_play(&board, &board_ext, &play);
                        } else {
                            fprintf(fd, "# Invalid play detected\n");
                            fflush(fd);
                            if (extend_uci) {
                                uci_game_in_error_state = 1;
                            }
                            break;
                        }
                    }
                }
            }
            continue;
        }
        if (extend_uci) {
            if (strcmp(buffer, "log_fen") == 0) {
                board_to_fen(buffer, &board, &board_ext);
                fprintf(fd, "# %s\n", buffer);
                fflush(fd);
                continue;
            }
        }
        if (strcmp(buffer, "go") == 0 || strncmp(buffer, "go ", strlen("go ")) == 0) {
            if (uci_game_in_error_state) {
                if (extend_uci) {
                    send_uci_command(fd, "error");
                }
                continue;
            }

            play_t play;

            set_search_limits(buffer);

            int played = ai_play(&play);
            if (played == CHECK_MATE || played == DRAW) {
                fprintf(fd, played == CHECK_MATE ? "# Player lost.\n" : "# Game is drawn.\n");
                fflush(fd);

                if (extend_uci) {
                    send_uci_command(fd, played == CHECK_MATE ? "loss" : "draw");
                } else {
                    // There is no play to make, but go is still owed an answer, and this is
                    // what the protocol says when there is none.
                    send_uci_command(fd, "bestmove 0000");
                }
                continue;
            }

            char * b = buffer + sprintf(buffer, "bestmove %c%d%c%d", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            if (play.promotion_option == PROMOTION_QUEEN) {
                sprintf(b, "q");
            } else if (play.promotion_option == PROMOTION_KNIGHT) {
                sprintf(b, "n");
            } else if (play.promotion_option == PROMOTION_BISHOP) {
                sprintf(b, "b");
            } else if (play.promotion_option == PROMOTION_ROOK) {
                sprintf(b, "r");
            }
            send_uci_command(fd, buffer);
            continue;
        }
    }

    if (fd != stderr) {
        fclose(fd);
    }
}

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
    populate_pawn_capture_masks();
    populate_knight_moves_masks();
    populate_king_moves_masks();
    populate_zobrist_masks();
    hash_table = calloc(HASH_TABLE_SIZE, sizeof(hash_table_entry_t));
    if (hash_table == NULL) {
        fprintf(stderr, "Could not allocate the %zu MB transposition table.\n", ((size_t)HASH_TABLE_SIZE * sizeof(hash_table_entry_t)) / (1024 * 1024));
        return EXIT_FAILURE;
    }

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
