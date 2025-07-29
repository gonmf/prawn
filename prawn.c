// TODO: implement correctly castling (no threats to path of king)

#include "common.h"

static char input_buffer[1024];

static char past_fens[256][60];
static unsigned char past_plays_count;
static board_t board;
static char last_play_x = -1;
static char last_play_y = -1;

#define PIECE_SCORE_MULTIPLIER 256
#define NO_SCORE 2147483647
#define CHECK_MATE 2147483646
#define DRAW 2147483645

#define MAX(A,B) ((A) > (B) ? (A) : (B))
#define MIN(A,B) ((A) < (B) ? (A) : (B))

#define QUEUE_PLAY(TO_X, TO_Y) \
    valid_plays[valid_plays_i].from_x = x; \
    valid_plays[valid_plays_i].from_y = y; \
    valid_plays[valid_plays_i].to_x = (TO_X); \
    valid_plays[valid_plays_i].to_y = (TO_Y); \
    valid_plays_i = valid_plays_i + 1;

static int determine_color(char c) {
    if (c == ' ') {
        return NO_COLOR;
    }
    return c > 'a' ? BLACK_COLOR : WHITE_COLOR;
}

static void print_board() {
    board_to_fen(input_buffer, &board);
    printf("%s\n", input_buffer);
    printf("╔═══╤═══╤═══╤═══╤═══╤═══╤═══╤═══╗┈╮\n");
    for (int y = 0; y < 8; y++) {
        printf("║ ");
        for (int x = 0; x < 8; x++) {
            if (x == last_play_x && y == last_play_y) {
                printf("\033[32m%c\e[0m", board.b[y * 8 + x]);
            } else {
                printf("%c", board.b[y * 8 + x]);
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

static int opposite_color(int color) {
    return color == WHITE_COLOR ? BLACK_COLOR : WHITE_COLOR;
}

static int just_play(board_t * board, const play_t * play, int score) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    char to_x = play->to_x;
    char to_y = play->to_y;
    char promotion_option = play->promotion_option;

    switch (board->b[to_y * 8 + to_x]) {
        case 'P':
           score -= 1 * PIECE_SCORE_MULTIPLIER;
           break;
        case 'p':
           score += 1 * PIECE_SCORE_MULTIPLIER;
           break;
        case 'N':
        case 'B':
            score -= 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'n':
        case 'b':
            score += 3 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'R':
            score -= 5 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'r':
            score += 5 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'Q':
            score -= 9 * PIECE_SCORE_MULTIPLIER;
            break;
        case 'q':
            score += 9 * PIECE_SCORE_MULTIPLIER;
            break;
    }

    if (board->en_passant_x != NO_EN_PASSANT) {
        if (board->en_passant_x == to_x) {
            if (from_y == 3 && board->b[from_y * 8 + from_x] == 'P') {
                board->b[from_y * 8 + to_x] = ' ';
                score += 1 * PIECE_SCORE_MULTIPLIER;
            } else if (from_y == 4 && board->b[from_y * 8 + from_x] == 'p') {
                board->b[from_y * 8 + to_x] = ' ';
                score -= 1 * PIECE_SCORE_MULTIPLIER;
            }
        }
        board->en_passant_x = NO_EN_PASSANT;
    }

    if (board->b[from_y * 8 + from_x] == 'K') {
        if (from_y == to_y) {
            if (board->white_right_castling && from_x + 2 == to_x && board->b[from_y * 8 + from_x + 1] == ' ' && board->b[from_y * 8 + from_x + 2] == ' ') {
                board->b[to_y * 8 + to_x - 1] = 'R';
                board->b[to_y * 8 + to_x + 1] = ' ';
            } else if (board->white_left_castling && from_x - 2 == to_x && board->b[from_y * 8 + from_x - 1] == ' ' && board->b[from_y * 8 + from_x - 2] == ' ' && board->b[from_y * 8 + from_x - 3] == ' ') {
                board->b[to_y * 8 + to_x - 2] = ' ';
                board->b[to_y * 8 + to_x + 1] = 'R';
            }
        }
        board->white_right_castling = 0;
        board->white_left_castling = 0;
    } else if (board->b[from_y * 8 + from_x] == 'k') {
        if (from_y == to_y) {
            if (board->black_right_castling && from_x + 2 == to_x && board->b[from_y * 8 + from_x + 1] == ' ' && board->b[from_y * 8 + from_x + 2] == ' ') {
                board->b[to_y * 8 + to_x - 1] = 'r';
                board->b[to_y * 8 + to_x + 1] = ' ';
            } else if (board->black_left_castling && from_x - 2 == to_x && board->b[from_y * 8 + from_x - 1] == ' ' && board->b[from_y * 8 + from_x - 2] == ' ' && board->b[from_y * 8 + from_x - 3] == ' ') {
                board->b[to_y * 8 + to_x - 2] = ' ';
                board->b[to_y * 8 + to_x + 1] = 'r';
            }
        }
        board->black_right_castling = 0;
        board->black_left_castling = 0;
    }

    if ((from_x == 0 && from_y == 7) || (to_x == 0 && to_y == 7)) {
        board->white_left_castling = 0;
    } else if ((from_x == 7 && from_y == 7) || (to_x == 7 && to_y == 7)) {
        board->white_right_castling = 0;
    } else if ((from_x == 0 && from_y == 0) || (to_x == 0 && to_y == 0)) {
        board->black_left_castling = 0;
    } else if ((from_x == 7 && from_y == 0) || (to_x == 7 && to_y == 0)) {
        board->black_right_castling = 0;
    }

    if (board->b[from_y * 8 + from_x] == 'P') {
        if (to_y == 0) {
            if (promotion_option == PROMOTION_QUEEN) {
                board->b[to_y * 8 + to_x] = 'Q';
                // 1 to 9
                score += 8 * PIECE_SCORE_MULTIPLIER;
            } else if (promotion_option == PROMOTION_KNIGHT) {
                board->b[to_y * 8 + to_x] = 'N';
                // 1 to 3
                score += 2 * PIECE_SCORE_MULTIPLIER;
            } else if (promotion_option == PROMOTION_BISHOP) {
                board->b[to_y * 8 + to_x] = 'B';
                // 1 to 3
                score += 2 * PIECE_SCORE_MULTIPLIER;
            } else {
                board->b[to_y * 8 + to_x] = 'R';
                // 1 to 5
                score += 4 * PIECE_SCORE_MULTIPLIER;
            }
        } else {
            if (from_y == 6 && to_y == 4) {
                board->en_passant_x = from_x;
            }
            board->b[to_y * 8 + to_x] = board->b[from_y * 8 + from_x];
        }
    } else if (board->b[from_y * 8 + from_x] == 'p') {
        if (to_y == 7) {
            if (promotion_option == PROMOTION_QUEEN) {
                board->b[to_y * 8 + to_x] = 'q';
                // 1 to 9
                score -= 8 * PIECE_SCORE_MULTIPLIER;
            } else if (promotion_option == PROMOTION_KNIGHT) {
                board->b[to_y * 8 + to_x] = 'n';
                // 1 to 3
                score -= 2 * PIECE_SCORE_MULTIPLIER;
            } else if (promotion_option == PROMOTION_BISHOP) {
                board->b[to_y * 8 + to_x] = 'b';
                // 1 to 3
                score -= 2 * PIECE_SCORE_MULTIPLIER;
            } else {
                board->b[to_y * 8 + to_x] = 'r';
                // 1 to 5
                score -= 4 * PIECE_SCORE_MULTIPLIER;
            }
        } else {
            if (from_y == 1 && to_y == 3) {
                board->en_passant_x = from_x;
            }
            board->b[to_y * 8 + to_x] = board->b[from_y * 8 + from_x];
        }
    } else {
        board->b[to_y * 8 + to_x] = board->b[from_y * 8 + from_x];
    }
    board->b[from_y * 8 + from_x] = ' ';

    board->color = opposite_color(board->color);

    return score;
}

static void actual_play(const play_t * play) {
    board_to_short_string(past_fens[past_plays_count++], &board);

    char moving_piece = board.b[play->from_y * 8 + play->from_x];
    if (moving_piece == 'P' || moving_piece == 'p' || board.b[play->to_y * 8 + play->to_x] != ' ') {
        board.halfmoves = 0;
    } else {
        board.halfmoves++;
    }

    last_play_x = play->to_x;
    last_play_y = play->to_y;
    if (board.color == BLACK_COLOR) {
        board.fullmoves++;
    }

    just_play(&board, play, 0);
}

static int enumerate_all_possible_plays_white(play_t * valid_plays, board_t * board) {
    int valid_plays_i = 0;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            char piece = board->b[y * 8 + x];
            if (determine_color(piece) != WHITE_COLOR) {
                continue;
            }

            valid_plays[valid_plays_i].promotion_option = 0;

            if (piece == 'P') {
                if (board->b[(y - 1) * 8 + x] == ' ') {
                    if (y == 1) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y - 1);
                    } else {
                        QUEUE_PLAY(x, y - 1);
                        if (y == 6 && board->b[(y - 2) * 8 + x] == ' ') {
                            QUEUE_PLAY(x, y - 2);
                        }
                    }
                }
                if (x < 7 && determine_color(board->b[(y - 1) * 8 + x + 1]) == BLACK_COLOR) {
                    if (y == 1) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x + 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x + 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x + 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x + 1, y - 1);
                    } else {
                        QUEUE_PLAY(x + 1, y - 1);
                    }
                }
                if (x > 0 && determine_color(board->b[(y - 1) * 8 + x - 1]) == BLACK_COLOR) {
                    if (y == 1) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x - 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x - 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x - 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x - 1, y - 1);
                    } else {
                        QUEUE_PLAY(x - 1, y - 1);
                    }
                }
                if (y == 3 && board->en_passant_x != NO_EN_PASSANT) {
                    if (board->en_passant_x == x - 1) {
                        QUEUE_PLAY(x - 1, y - 1);
                    } else if (board->en_passant_x == x + 1) {
                        QUEUE_PLAY(x + 1, y - 1);
                    }
                }
                continue;
            }
            if (piece == 'N') {
                char x2 = x - 1;
                char y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 1;
                y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != WHITE_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                continue;
            }
            if (piece == 'R' || piece == 'Q') {
                for (char dst = 1; x - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x - dst]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x - dst, y);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x + dst]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x + dst, y);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x, y - dst);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x, y + dst);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                if (piece == 'R') {
                    continue;
                }
            }
            if (piece == 'B' || piece == 'Q') {
                for (char dst = 1; x - dst >= 0 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x - dst]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x - dst, y - dst);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x + dst]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x + dst, y - dst);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x - dst >= 0 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x - dst]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x - dst, y + dst);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x + dst]);
                    if (piece_color != WHITE_COLOR) {
                        QUEUE_PLAY(x + dst, y + dst);
                        if (piece_color == BLACK_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                continue;
            }
            if (piece == 'K') {
                if (x - 1 >= 0) {
                    if (determine_color(board->b[y * 8 + x - 1]) != WHITE_COLOR) {
                        QUEUE_PLAY(x - 1, y);
                    }
                    if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x - 1]) != WHITE_COLOR) {
                        QUEUE_PLAY(x - 1, y - 1);
                    }
                    if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x - 1]) != WHITE_COLOR) {
                        QUEUE_PLAY(x - 1, y + 1);
                    }
                }
                if (x + 1 <= 7) {
                    if (determine_color(board->b[y * 8 + x + 1]) != WHITE_COLOR) {
                        QUEUE_PLAY(x + 1, y);
                    }
                    if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x + 1]) != WHITE_COLOR) {
                        QUEUE_PLAY(x + 1, y - 1);
                    }
                    if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x + 1]) != WHITE_COLOR) {
                        QUEUE_PLAY(x + 1, y + 1);
                    }
                }
                if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x]) != WHITE_COLOR) {
                    QUEUE_PLAY(x, y - 1);
                }
                if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x]) != WHITE_COLOR) {
                    QUEUE_PLAY(x, y + 1);
                }
                if (board->white_right_castling && board->b[7 * 8 + 4 + 1] == ' ' && board->b[7 * 8 + 4 + 2] == ' ') {
                    QUEUE_PLAY(x + 2, y);
                }
                if (board->white_left_castling && board->b[7 * 8 + 4 - 1] == ' ' && board->b[7 * 8 + 4 - 2] == ' ' && board->b[7 * 8 + 4 - 3] == ' ') {
                    QUEUE_PLAY(x - 2, y);
                }
                continue;
            }
        }
    }

    return valid_plays_i;
}

static int enumerate_all_possible_plays_black(play_t * valid_plays, board_t * board) {
    int valid_plays_i = 0;

    for (int y = 7; y >= 0; y--) {
        for (int x = 0; x < 8; x++) {
            char piece = board->b[y * 8 + x];
            if (determine_color(piece) != BLACK_COLOR) {
                continue;
            }

            valid_plays[valid_plays_i].promotion_option = 0;

            if (piece == 'p') {
                if (board->b[(y + 1) * 8 + x] == ' ') {
                    if (y == 6) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y + 1);
                    } else {
                        QUEUE_PLAY(x, y + 1);
                        if (y == 1 && board->b[(y + 2) * 8 + x] == ' ') {
                            QUEUE_PLAY(x, y + 2);
                        }
                    }
                }
                if (x < 7 && determine_color(board->b[(y + 1) * 8 + x + 1]) == WHITE_COLOR) {
                    if (y == 6) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x + 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x + 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x + 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x + 1, y + 1);
                    } else {
                        QUEUE_PLAY(x + 1, y + 1);
                    }
                }
                if (x > 0 && determine_color(board->b[(y + 1) * 8 + x - 1]) == WHITE_COLOR) {
                    if (y == 6) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x - 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x - 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x - 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x - 1, y + 1);
                    } else {
                        QUEUE_PLAY(x - 1, y + 1);
                    }
                }
                if (y == 4 && board->en_passant_x != NO_EN_PASSANT) {
                    if (board->en_passant_x == x - 1) {
                        QUEUE_PLAY(x - 1, y + 1);
                    } else if (board->en_passant_x == x + 1) {
                        QUEUE_PLAY(x + 1, y + 1);
                    }
                }
                continue;
            }
            if (piece == 'n') {
                char x2 = x - 1;
                char y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 1;
                y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != BLACK_COLOR) {
                    QUEUE_PLAY(x2, y2);
                }
                continue;
            }
            if (piece == 'r' || piece == 'q') {
                for (char dst = 1; x - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x - dst]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x - dst, y);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x + dst]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x + dst, y);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x, y - dst);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x, y + dst);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                if (piece == 'r') {
                    continue;
                }
            }
            if (piece == 'b' || piece == 'q') {
                for (char dst = 1; x - dst >= 0 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x - dst]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x - dst, y - dst);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x + dst]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x + dst, y - dst);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x - dst >= 0 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x - dst]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x - dst, y + dst);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x + dst]);
                    if (piece_color != BLACK_COLOR) {
                        QUEUE_PLAY(x + dst, y + dst);
                        if (piece_color == WHITE_COLOR) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                continue;
            }
            if (piece == 'k') {
                if (x - 1 >= 0) {
                    if (determine_color(board->b[y * 8 + x - 1]) != BLACK_COLOR) {
                        QUEUE_PLAY(x - 1, y);
                    }
                    if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x - 1]) != BLACK_COLOR) {
                        QUEUE_PLAY(x - 1, y - 1);
                    }
                    if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x - 1]) != BLACK_COLOR) {
                        QUEUE_PLAY(x - 1, y + 1);
                    }
                }
                if (x + 1 <= 7) {
                    if (determine_color(board->b[y * 8 + x + 1]) != BLACK_COLOR) {
                        QUEUE_PLAY(x + 1, y);
                    }
                    if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x + 1]) != BLACK_COLOR) {
                        QUEUE_PLAY(x + 1, y - 1);
                    }
                    if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x + 1]) != BLACK_COLOR) {
                        QUEUE_PLAY(x + 1, y + 1);
                    }
                }
                if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x]) != BLACK_COLOR) {
                    QUEUE_PLAY(x, y - 1);
                }
                if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x]) != BLACK_COLOR) {
                    QUEUE_PLAY(x, y + 1);
                }
                if (board->black_right_castling && board->b[0 * 8 + 4 + 1] == ' ' && board->b[0 * 8 + 4 + 2] == ' ') {
                    QUEUE_PLAY(x + 2, y);
                }
                if (board->black_left_castling && board->b[0 * 8 + 4 - 1] == ' ' && board->b[0 * 8 + 4 - 2] == ' ' && board->b[0 * 8 + 4 - 3] == ' ') {
                    QUEUE_PLAY(x - 2, y);
                }
                continue;
            }
        }
    }

    return valid_plays_i;
}

static int enumerate_possible_capture_plays(play_t * valid_plays, board_t * board) {
    int valid_plays_i = 0;
    int opnt_color = opposite_color(board->color);

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            char piece = board->b[y * 8 + x];
            if (determine_color(piece) != board->color) {
                continue;
            }

            if (piece == 'P') {
                if (x < 7 && determine_color(board->b[(y - 1) * 8 + x + 1]) == opnt_color) {
                    QUEUE_PLAY(x + 1, y - 1);
                }
                if (x > 0 && determine_color(board->b[(y - 1) * 8 + x - 1]) == opnt_color) {
                    QUEUE_PLAY(x - 1, y - 1);
                }
                continue;
            }
            if (piece == 'p') {
                if (x < 7 && determine_color(board->b[(y + 1) * 8 + x + 1]) == opnt_color) {
                    QUEUE_PLAY(x + 1, y + 1);
                }
                if (x > 0 && determine_color(board->b[(y + 1) * 8 + x - 1]) == opnt_color) {
                    QUEUE_PLAY(x - 1, y + 1);
                }
                continue;
            }
            if (piece == 'N' || piece == 'n') {
                char x2 = x - 1;
                char y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 1;
                y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x - 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                x2 = x + 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) == opnt_color) {
                    QUEUE_PLAY(x2, y2);
                }
                continue;
            }
            if (piece == 'R' || piece == 'r' || piece == 'Q' || piece == 'q') {
                for (char dst = 1; x - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x - dst]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x - dst, y);
                    }
                    break;
                }
                for (char dst = 1; x + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x + dst]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x + dst, y);
                    }
                    break;
                }
                for (char dst = 1; y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x, y - dst);
                    }
                    break;
                }
                for (char dst = 1; y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x, y + dst);
                    }
                    break;
                }
                if (piece == 'R' || piece == 'r') {
                    continue;
                }
            }
            if (piece == 'B' || piece == 'b' || piece == 'Q' || piece == 'q') {
                for (char dst = 1; x - dst >= 0 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x - dst]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x - dst, y - dst);
                    }
                    break;
                }
                for (char dst = 1; x + dst <= 7 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x + dst]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x + dst, y - dst);
                    }
                    break;
                }
                for (char dst = 1; x - dst >= 0 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x - dst]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x - dst, y + dst);
                    }
                    break;
                }
                for (char dst = 1; x + dst <= 7 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x + dst]);
                    if (piece_color == NO_COLOR) {
                        continue;
                    }
                    if (piece_color == opnt_color) {
                        QUEUE_PLAY(x + dst, y + dst);
                    }
                    break;
                }
                continue;
            }
            if (piece == 'K' || piece == 'k') {
                if (x - 1 >= 0) {
                    if (determine_color(board->b[y * 8 + x - 1]) == opnt_color) {
                        QUEUE_PLAY(x - 1, y);
                    }
                    if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x - 1]) == opnt_color) {
                        QUEUE_PLAY(x - 1, y - 1);
                    }
                    if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x - 1]) == opnt_color) {
                        QUEUE_PLAY(x - 1, y + 1);
                    }
                }
                if (x + 1 <= 7) {
                    if (determine_color(board->b[y * 8 + x + 1]) == opnt_color) {
                        QUEUE_PLAY(x + 1, y);
                    }
                    if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x + 1]) == opnt_color) {
                        QUEUE_PLAY(x + 1, y - 1);
                    }
                    if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x + 1]) == opnt_color) {
                        QUEUE_PLAY(x + 1, y + 1);
                    }
                }
                if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x]) == opnt_color) {
                    QUEUE_PLAY(x, y - 1);
                }
                if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x]) == opnt_color) {
                    QUEUE_PLAY(x, y + 1);
                }
                continue;
            }
        }
    }

    return valid_plays_i;
}

static int enumerate_legal_plays(play_t * valid_plays, board_t * board) {
    int valid_plays_i = 0;
    play_t valid_plays_local[128];
    int valid_plays_local_i = board->color == WHITE_COLOR ? enumerate_all_possible_plays_white(valid_plays_local, board) : enumerate_all_possible_plays_black(valid_plays_local, board);
    char king_piece = board->color == WHITE_COLOR ? 'K' : 'k';

    board_t board_cpy;
    play_t cpy_valid_plays[128];

    for (int i = 0; i < valid_plays_local_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));
        just_play(&board_cpy, &valid_plays_local[i], 0);

        int cpy_valid_plays_i = enumerate_possible_capture_plays(cpy_valid_plays, &board_cpy);

        int play_is_valid = 1;
        for (int j = 0; j < cpy_valid_plays_i; ++j) {
            char x = cpy_valid_plays[j].to_x;
            char y = cpy_valid_plays[j].to_y;

            if (board_cpy.b[y * 8 + x] == king_piece) {
                play_is_valid = 0;
                break;
            }
        }
        if (play_is_valid) {
            valid_plays[valid_plays_i] = valid_plays_local[i];
            valid_plays_i++;
        }
    }

    return valid_plays_i;
}

static int king_threatened(board_t * board) {
    char king_piece = board->color == WHITE_COLOR ? 'K' : 'k';
    play_t valid_plays[128];

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
    board->color = opposite_color(board->color);

    int valid_plays_i = enumerate_possible_capture_plays(valid_plays, board);

    board->en_passant_x = en_passant_x;
    board->white_left_castling = white_left_castling;
    board->white_right_castling = white_right_castling;
    board->black_left_castling = black_left_castling;
    board->black_right_castling = black_right_castling;
    board->color = opposite_color(board->color);

    for (int i = 0; i < valid_plays_i; ++i) {
        char x = valid_plays[i].to_x;
        char y = valid_plays[i].to_y;

        if (board->b[y * 8 + x] == king_piece) {
            return 1;
        }
    }

    return 0;
}

static int minimax(board_t * board, int depth, int alpha, int beta, int initial_score) {
    if (board->halfmoves == 50) {
        return board->color != WHITE_COLOR ? 10000000 - depth * 128 : -10000000 + depth * 128;
    }
    if (depth == 0) {
        return initial_score;
    }

    board_t board_cpy;
    play_t valid_plays[128];

    int valid_plays_i = enumerate_legal_plays(valid_plays, board);
    if (valid_plays_i == 0) {
        if (king_threatened(board)) {
            return board->color != WHITE_COLOR ? 20000000 + depth * 128 : -20000000 - depth * 128;
        } else {
            return board->color != WHITE_COLOR ? 10000000 - depth * 128 : -10000000 + depth * 128;
        }
    }

    int best_score = NO_SCORE;

    for (int i = 0; i < valid_plays_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));

        int score = just_play(&board_cpy, &valid_plays[i], initial_score);
        int score_extra = board->color == WHITE_COLOR ? valid_plays_i : -valid_plays_i;

        score = minimax(&board_cpy, depth - 1, alpha, beta, score + score_extra);

        if (score != NO_SCORE) {
            if (board->color == WHITE_COLOR) {
                if (best_score == NO_SCORE || score > best_score) {
                    best_score = score;
                }
                if (score >= beta) {
                    break;
                }
                alpha = MAX(alpha, score);
            } else {
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

    return best_score;
}

static int ai_play(play_t * play) {
    board_t board_cpy;
    play_t valid_plays[128];
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

    for (int i = 0; i < valid_plays_i; ++i) {
        memcpy(&board_cpy, &board, sizeof(board_t));
        int score = just_play(&board_cpy, &valid_plays[i], 0);
        int score_extra = board.color == WHITE_COLOR ? valid_plays_i : -valid_plays_i;

        board_to_short_string(input_buffer, &board_cpy);
        int position_repeated = 0;
        for (int pos = 0; pos < past_plays_count; ++pos) {
            if (strcmp(past_fens[pos], input_buffer) == 0) {
                position_repeated++;
                if (position_repeated == 2) {
                    break;
                }
            }
        }
        if (position_repeated == 2) {
            score = board_cpy.color == WHITE_COLOR ? 10000000 + 5 * 128 : -10000000 - 5 * 128;
        } else {
            score = minimax(&board_cpy, 5, alpha, beta, score + score_extra);
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
        fgets(input_buffer, 1024, stdin);

        if (input_buffer[0] == 'q' || input_buffer[0] == 'Q') {
            return PROMOTION_QUEEN;
        }
        if (input_buffer[0] == 'n' || input_buffer[0] == 'N') {
            return PROMOTION_KNIGHT;
        }
        if (input_buffer[0] == 'b' || input_buffer[0] == 'B') {
            return PROMOTION_BISHOP;
        }
        if (input_buffer[0] == 'r' || input_buffer[0] == 'R') {
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
        fgets(input_buffer, 1024, stdin);

        input_buffer[4] = 0;
        char * input_is_valid = read_play(play, input_buffer);

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
                char from_piece = board.b[play->from_y * 8 + play->from_x];
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
    memcpy(board.b, "rnbqkbnrpppppppp                                PPPPPPPPRNBQKBNR", 64);
    board.en_passant_x = NO_EN_PASSANT;
    board.white_left_castling = 1;
    board.white_right_castling = 1;
    board.black_left_castling = 1;
    board.black_right_castling = 1;
    board.color = WHITE_COLOR;
    board.halfmoves = 0;
    board.fullmoves = 0;
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
        print_board();
        printf("%s to play...\n\n", board.color == WHITE_COLOR ? "White" : "Black");

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
        fgets(input_buffer, 1024, stdin);

        if (strcmp(input_buffer, "1\n") == 0) {
            player_one_is_human = 1;
            player_two_is_human = 0;
            break;
        }
        if (strcmp(input_buffer, "2\n") == 0) {
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
        if (fgets(input_buffer, 1024, stdin) == NULL) {
            break;
        }
        for (int i = 0; i < 1024; ++i) {
            if (input_buffer[i] == '\n') {
                input_buffer[i] = 0;
                break;
            }
        }

        fprintf(fd, "> %s\n", input_buffer);
        fflush(fd);

        if (feof(fd) || ferror(fd) || strcmp(input_buffer, "quit") == 0) {
            break;
        }
        if (strcmp(input_buffer, "uci") == 0) {
            send_uci_command(fd, "id name prawn 1.0");
            send_uci_command(fd, "id author gonmf");
            send_uci_command(fd, "uciok");
            continue;
        }
        if (strcmp(input_buffer, "isready") == 0) {
            send_uci_command(fd, "readyok");
            continue;
        }
        if (strcmp(input_buffer, "ucinewgame") == 0) {
            reset_board();
            continue;
        }
        if (strncmp(input_buffer, "position ", strlen("position ")) == 0) {
            char * str = strstr(input_buffer, " startpos ");
            if (str) {
                reset_board();
            } else {
                str = strstr(input_buffer, " fen ");
                if (str) {
                    fen_to_board(&board, str + strlen(" fen "));
                }
            }

            str = strstr(input_buffer, " moves ");
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
        if (strncmp(input_buffer, "go ", strlen("go ")) == 0) {
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

            if (play.promotion_option == PROMOTION_QUEEN) {
                sprintf(input_buffer, "bestmove %c%d%c%dq", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            } else if (play.promotion_option == PROMOTION_KNIGHT) {
                sprintf(input_buffer, "bestmove %c%d%c%dn", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            } else if (play.promotion_option == PROMOTION_BISHOP) {
                sprintf(input_buffer, "bestmove %c%d%c%db", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            } else if (play.promotion_option == PROMOTION_ROOK) {
                sprintf(input_buffer, "bestmove %c%d%c%dr", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            } else {
                sprintf(input_buffer, "bestmove %c%d%c%d", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            }
            send_uci_command(fd, input_buffer);
            continue;
        }
    }

    fclose(fd);
}

static void show_help() {
    printf("Prawn %s\n\nOptions:\n", PROGRAM_VERSION);
    printf("  --uci  - Start on UCI intergace mode (default)\n");
    printf("  --text - Play via the text interface\n");
    printf("  --self - Have the program play against itself\n");
    printf("  --help - Show this message\n\n");
}

int main(int argc, char * argv[]) {
    reset_board();

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--uci") == 0) {
            uci_mode();
            return 0;
        }
        if (strcmp(argv[i], "--text") == 0) {
            text_mode();
            return 0;
        }
        if (strcmp(argv[i], "--self") == 0) {
            self_play();
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0) {
            show_help();
            return 0;
        }
    }

    uci_mode();
    return 0;
}
