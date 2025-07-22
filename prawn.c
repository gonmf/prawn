#include "common.h"

static char input_buffer[1024];

static board_t board;
static char last_play_x = -1;
static char last_play_y = -1;
static char player_color = WHITE_COLOR;

#define PIECE_SCORE_MULTIPLIER 256
#define NO_SCORE 2147483647
#define CHECK_MATE 2147483646
#define DRAW 2147483645

#define MAX(A,B) ((A) > (B) ? (A) : (B))
#define MIN(A,B) ((A) < (B) ? (A) : (B))

static int max_valid_plays = 0;

#define QUEUE_PLAY(FROM_X, FROM_Y, TO_X, TO_Y) \
    valid_plays[valid_plays_i].from_x = (FROM_X); \
    valid_plays[valid_plays_i].from_y = (FROM_Y); \
    valid_plays[valid_plays_i].to_x = (TO_X); \
    valid_plays[valid_plays_i].to_y = (TO_Y); \
    valid_plays_i = valid_plays_i + 1; \
    if (valid_plays_i == 64) { \
        return valid_plays_i; \
    }

static int determine_color(char c) {
    if (c == ' ') {
        return NO_COLOR;
    }
    return c > 'a' ? BLACK_COLOR : WHITE_COLOR;
}

static void print_board() {
    board_to_fen(input_buffer, &board, player_color);
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
    printf("╰┈a┈┈┈b┈┈┈c┈┈┈d┈┈┈e┈┈┈f┈┈┈g┈┈┈h┈┈┈╯\n");
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

    return score;
}

static void actual_play(const play_t * play) {
    last_play_x = play->to_x;
    last_play_y = play->to_y;
    just_play(&board, play, 0);
    if (player_color == BLACK_COLOR) {
      board.fullmoves++;
    }
    player_color = opposite_color(player_color);
}

static int enumerate_possible_plays(play_t * valid_plays, board_t * board, int color) {
    int valid_plays_i = 0;
    int opnt_color = opposite_color(color);

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            char piece = board->b[y * 8 + x];
            if (determine_color(piece) != color) {
                continue;
            }

            if (piece == 'P') {
                if (board->b[(y - 1) * 8 + x] == ' ') {
                    if (y == 1) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y, x, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y, x, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y, x, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y, x, y - 1);
                    } else {
                        QUEUE_PLAY(x, y, x, y - 1);
                        if (y == 6 && board->b[(y - 2) * 8 + x] == ' ') {
                            QUEUE_PLAY(x, y, x, y - 2);
                        }
                    }
                }
                if (x < 7 && determine_color(board->b[(y - 1) * 8 + x + 1]) == opnt_color) {
                    if (y == 1) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y, x + 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y, x + 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y, x + 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y, x + 1, y - 1);
                    } else {
                        QUEUE_PLAY(x, y, x + 1, y - 1);
                    }
                }
                if (x > 0 && determine_color(board->b[(y - 1) * 8 + x - 1]) == opnt_color) {
                    if (y == 1) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y, x - 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y, x - 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y, x - 1, y - 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y, x - 1, y - 1);
                    } else {
                        QUEUE_PLAY(x, y, x - 1, y - 1);
                    }
                }
                if (y == 3 && board->en_passant_x != NO_EN_PASSANT) {
                    if (board->en_passant_x == x - 1) {
                        QUEUE_PLAY(x, y, x - 1, y - 1);
                    } else if (board->en_passant_x == x + 1) {
                        QUEUE_PLAY(x, y, x + 1, y - 1);
                    }
                }
                continue;
            }
            if (piece == 'p') {
                if (board->b[(y + 1) * 8 + x] == ' ') {
                    if (y == 6) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y, x, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y, x, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y, x, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y, x, y + 1);
                    } else {
                        QUEUE_PLAY(x, y, x, y + 1);
                        if (y == 1 && board->b[(y + 2) * 8 + x] == ' ') {
                            QUEUE_PLAY(x, y, x, y + 2);
                        }
                    }
                }
                if (x < 7 && determine_color(board->b[(y + 1) * 8 + x + 1]) == opnt_color) {
                    if (y == 6) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y, x + 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y, x + 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y, x + 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y, x + 1, y + 1);
                    } else {
                        QUEUE_PLAY(x, y, x + 1, y + 1);
                    }
                }
                if (x > 0 && determine_color(board->b[(y + 1) * 8 + x - 1]) == opnt_color) {
                    if (y == 6) {
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                        QUEUE_PLAY(x, y, x - 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                        QUEUE_PLAY(x, y, x - 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                        QUEUE_PLAY(x, y, x - 1, y + 1);
                        valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                        QUEUE_PLAY(x, y, x - 1, y + 1);
                    } else {
                        QUEUE_PLAY(x, y, x - 1, y + 1);
                    }
                }
                if (y == 4 && board->en_passant_x != NO_EN_PASSANT) {
                    if (board->en_passant_x == x - 1) {
                        QUEUE_PLAY(x, y, x - 1, y + 1);
                    } else if (board->en_passant_x == x + 1) {
                        QUEUE_PLAY(x, y, x + 1, y + 1);
                    }
                }
                continue;
            }
            if (piece == 'N' || piece == 'n') {
                char x2 = x - 1;
                char y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                x2 = x - 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                x2 = x + 1;
                y2 = y + 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                x2 = x + 1;
                y2 = y - 2;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                x2 = x - 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                x2 = x - 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                x2 = x + 2;
                y2 = y + 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                x2 = x + 2;
                y2 = y - 1;
                if (x2 >= 0 && x2 <= 7 && y2 >= 0 && y2 <= 7 && determine_color(board->b[y2 * 8 + x2]) != color) {
                    QUEUE_PLAY(x, y, x2, y2);
                }
                continue;
            }
            if (piece == 'R' || piece == 'r' || piece == 'Q' || piece == 'q') {
                for (char dst = 1; x - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x - dst]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x - dst, y);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[y * 8 + x + dst]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x + dst, y);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x, y - dst);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x, y + dst);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                if (piece == 'R' || piece == 'r') {
                    continue;
                }
            }
            if (piece == 'B' || piece == 'b' || piece == 'Q' || piece == 'q') {
                for (char dst = 1; x - dst >= 0 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x - dst]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x - dst, y - dst);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7 && y - dst >= 0; dst++) {
                    int piece_color = determine_color(board->b[(y - dst) * 8 + x + dst]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x + dst, y - dst);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x - dst >= 0 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x - dst]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x - dst, y + dst);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                for (char dst = 1; x + dst <= 7 && y + dst <= 7; dst++) {
                    int piece_color = determine_color(board->b[(y + dst) * 8 + x + dst]);
                    if (piece_color != color) {
                        QUEUE_PLAY(x, y, x + dst, y + dst);
                        if (piece_color == opnt_color) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                continue;
            }
            if (piece == 'K' || piece == 'k') {
                if (x - 1 >= 0 && determine_color(board->b[y * 8 + x - 1]) != color) {
                    QUEUE_PLAY(x, y, x - 1, y);
                }
                if (x + 1 <= 7 && determine_color(board->b[y * 8 + x + 1]) != color) {
                    QUEUE_PLAY(x, y, x + 1, y);
                }
                if (y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x]) != color) {
                    QUEUE_PLAY(x, y, x, y - 1);
                }
                if (y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x]) != color) {
                    QUEUE_PLAY(x, y, x, y + 1);
                }
                if (x - 1 >= 0 && y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x - 1]) != color) {
                    QUEUE_PLAY(x, y, x - 1, y - 1);
                }
                if (x + 1 <= 7 && y - 1 >= 0 && determine_color(board->b[(y - 1) * 8 + x + 1]) != color) {
                    QUEUE_PLAY(x, y, x + 1, y - 1);
                }
                if (x - 1 >= 0 && y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x - 1]) != color) {
                    QUEUE_PLAY(x, y, x - 1, y + 1);
                }
                if (x + 1 <= 7 && y + 1 <= 7 && determine_color(board->b[(y + 1) * 8 + x + 1]) != color) {
                    QUEUE_PLAY(x, y, x + 1, y + 1);
                }
                if (piece == 'K') {
                    if (board->white_right_castling && x == 4 && y == 7 && board->b[y * 8 + x + 1] == ' ' && board->b[y * 8 + x + 2] == ' ') {
                        QUEUE_PLAY(x, y, x + 2, y);
                    }
                    if (board->white_right_castling && x == 4 && y == 7 && board->b[y * 8 + x - 1] == ' ' && board->b[y * 8 + x - 2] == ' ' && board->b[y * 8 + x - 3] == ' ') {
                        QUEUE_PLAY(x, y, x - 2, y);
                    }
                } else {
                    if (board->white_right_castling && x == 4 && y == 0 && board->b[y * 8 + x + 1] == ' ' && board->b[y * 8 + x + 2] == ' ') {
                        QUEUE_PLAY(x, y, x + 2, y);
                    }
                    if (board->white_right_castling && x == 4 && y == 0 && board->b[y * 8 + x - 1] == ' ' && board->b[y * 8 + x - 2] == ' ' && board->b[y * 8 + x - 3] == ' ') {
                        QUEUE_PLAY(x, y, x - 2, y);
                    }
                }
                continue;
            }
        }
    }

    max_valid_plays = valid_plays_i > max_valid_plays ? valid_plays_i : max_valid_plays;

    return valid_plays_i;
}

static int enumerate_possible_plays_with_safe_king(play_t * valid_plays, board_t * board, int color) {
    int valid_plays_i = enumerate_possible_plays(valid_plays, board, color);
    char king_piece = color == WHITE_COLOR ? 'K' : 'k';

    board_t board_cpy;
    play_t cpy_valid_plays[64];

    for (int i = 0; i < valid_plays_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));
        just_play(&board_cpy, &valid_plays[i], 0);

        int cpy_valid_plays_i = enumerate_possible_plays(cpy_valid_plays, &board_cpy, opposite_color(color));

        for (int j = 0; j < cpy_valid_plays_i; ++j) {
            char x = cpy_valid_plays[j].to_x;
            char y = cpy_valid_plays[j].to_y;

            if (board_cpy.b[y * 8 + x] == king_piece) {
                valid_plays[i--] = valid_plays[--valid_plays_i];
                break;
            }
        }
    }

    return valid_plays_i;
}

static int king_threatened(board_t * board, int color) {
    char king_piece = color == WHITE_COLOR ? 'K' : 'k';
    play_t valid_plays[64];

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

    int valid_plays_i = enumerate_possible_plays(valid_plays, board, opposite_color(color));

    board->en_passant_x = en_passant_x;
    board->white_left_castling = white_left_castling;
    board->white_right_castling = white_right_castling;
    board->black_left_castling = black_left_castling;
    board->black_right_castling = black_right_castling;

    for (int i = 0; i < valid_plays_i; ++i) {
        char x = valid_plays[i].to_x;
        char y = valid_plays[i].to_y;

        if (board->b[y * 8 + x] == king_piece) {
            return 1;
        }
    }

    return 0;
}

static int minimax(board_t * board, int color, int depth, int alpha, int beta, int initial_score) {
    if (depth == 0) {
        return initial_score;
    }

    board_t board_cpy;
    play_t valid_plays[64];

    int valid_plays_i = enumerate_possible_plays_with_safe_king(valid_plays, board, color);
    if (valid_plays_i == 0) {
        if (king_threatened(board, color)) {
            return color != WHITE_COLOR ? 20000000 + depth * 128 : -20000000 - depth * 128;
        } else {
            return color != WHITE_COLOR ? 10000000 + depth * 128 : -10000000 - depth * 128;
        }
    }

    int best_score = NO_SCORE;

    for (int i = 0; i < valid_plays_i; ++i) {
        memcpy(&board_cpy, board, sizeof(board_t));

        int score = just_play(&board_cpy, &valid_plays[i], initial_score);
        int score_extra = color == WHITE_COLOR ? valid_plays_i : -valid_plays_i;

        score = minimax(&board_cpy, opposite_color(color), depth - 1, alpha, beta, score + score_extra);
        if (color == WHITE_COLOR) {
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

    return best_score;
}

static int ai_play(play_t * play) {
    board_t board_cpy;
    play_t valid_plays[64];
    int alpha = -2147483644;
    int beta = 2147483644;

    int valid_plays_i = enumerate_possible_plays_with_safe_king(valid_plays, &board, player_color);
    if (valid_plays_i == 0) {
        if (king_threatened(&board, player_color)) {
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
        int score_extra = player_color == WHITE_COLOR ? valid_plays_i : -valid_plays_i;

        score = minimax(&board_cpy, opposite_color(player_color), 5, alpha, beta, score + score_extra);

        if (player_color == WHITE_COLOR) {
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

static void input_play(play_t * play, const play_t * valid_plays, int valid_plays_i) {
    while (1) {
        printf("Input (example: e2e4): ");
        fgets(input_buffer, 1024, stdin);

        if (input_buffer[0] >= 'a' && input_buffer[0] <= 'h' && input_buffer[1] >= '1' && input_buffer[1] <= '8' && input_buffer[2] >= 'a' && input_buffer[2] <= 'h' && input_buffer[3] >= '1' && input_buffer[3] <= '8') {
            play->from_x = input_buffer[0] - 'a';
            play->from_y = '8' - input_buffer[1];
            play->to_x = input_buffer[2] - 'a';
            play->to_y = '8' - input_buffer[3];

            int valid_input = 0;
            for (int i = 0; i < valid_plays_i; ++i) {
                char from_x2 = valid_plays[i].from_x;
                char from_y2 = valid_plays[i].from_y;
                char to_x2 = valid_plays[i].to_x;
                char to_y2 = valid_plays[i].to_y;
                if (play->from_x == from_x2 && play->from_y == from_y2 && play->to_x == to_x2 && play->to_y == to_y2) {
                    valid_input = 1;
                    break;
                }
            }
            if (valid_input) {
                char from_piece = board.b[play->from_y * 8 + play->from_x];
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
    player_color = WHITE_COLOR;
    board.halfmoves = 0; // TODO:
    board.fullmoves = 0;
}

static void send_uci_command(FILE * fd, const char * str) {
    fprintf(fd, "< %s\n", str);
    fflush(fd);
    printf("%s\n", str);
    fflush(stdout);
}

static void text_mode() {
    printf("Prawn 1.0\n\n 1 - Human vs CPU\n 2 - CPU vs human\n 3 - Human vs human\n 4 - CPU vs CPU\n\n");

    int player_one_is_human;
    int player_two_is_human;
    play_t valid_plays[64];

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
        if (strcmp(input_buffer, "3\n") == 0) {
            player_one_is_human = 1;
            player_two_is_human = 1;
            break;
        }
        if (strcmp(input_buffer, "4\n") == 0) {
            player_one_is_human = 0;
            player_two_is_human = 0;
            break;
        }
    }

    printf("\nNew game - %s vs %s\n\n", player_one_is_human ? "human" : "cpu", player_two_is_human ? "human" : "cpu");

    while (1) {
        print_board();
        printf("\n%s to play...\n\n", player_color == WHITE_COLOR ? "White" : "Black");

        int player_is_human = player_color == WHITE_COLOR ? player_one_is_human : player_two_is_human;

        if (player_is_human) {
            int valid_plays_i = enumerate_possible_plays_with_safe_king(valid_plays, &board, player_color);
            if (valid_plays_i == 0) {
                if (king_threatened(&board, player_color)) {
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

static void uci_mode() {
    time_t t;
    struct tm * tm_info;
    time(&t);
    tm_info = localtime(&t);
    strftime(input_buffer, 1024, "log-%Y-%m-%d_%H-%M-%S.txt", tm_info);
    FILE * fd = fopen(input_buffer, "w");

    fprintf(fd, "Starting in UCI mode.\n");
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
        if (strncmp(input_buffer, "position fen ", strlen("position fen ")) == 0) {
            fen_to_board(&board, &player_color, input_buffer + strlen("position fen "));
            continue;
        }
        if (strncmp(input_buffer, "go ", strlen("go ")) == 0) {
            fprintf(fd, "Thinking...\n");
            fflush(fd);

            play_t play;
            int played = ai_play(&play);
            if (played == CHECK_MATE) {
                fprintf(fd, "Player lost. Closing.\n");
                fflush(fd);
                break;
            }
            if (played == DRAW) {
                fprintf(fd, "Game is drawn. Closing.\n");
                fflush(fd);
                break;
            }

            fprintf(fd, "Finished thinking.\n");
            fflush(fd);

            sprintf(input_buffer, "bestmove %c%d%c%d\n", 'a' + play.from_x, 8 - play.from_y, 'a' + play.to_x, 8 - play.to_y);
            send_uci_command(fd, input_buffer);
            continue;
        }
    }

    fprintf(fd, "Program is closing.\n");
    fflush(fd);

    fclose(fd);
}

static void show_help() {
    printf("Prawn 1.0\n\nUse option --text for the text interface, otherwise the program starts in UCI mode.\n\n");
}

int main(int argc, char * argv[]) {
    reset_board();

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help();
            return 0;
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--text") == 0) {
            text_mode();
            return 0;
        }
    }

    uci_mode();
    return 0;
}
