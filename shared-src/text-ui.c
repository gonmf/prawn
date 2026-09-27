#include "text_ui.h"

void fprint_board(FILE * fd, const board_t * board, const board_ext_t * board_ext) {
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

void print_board(const board_t * board, const board_ext_t * board_ext) {
    fprint_board(stdout, board, board_ext);
}

void read_input_line() {
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        printf("\n");
        exit(EXIT_SUCCESS);
    }
}

char input_promotion_piece() {
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

char * read_play(play_t * play, char * str) {
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

int input_play(play_t * play, const play_t * valid_plays, int valid_plays_i) {
    while (1) {
        printf("Input (example: e2e4): ");
        read_input_line();

        buffer[5] = 0;
        if (strcmp(buffer, "quit\n") == 0) {
            exit(EXIT_SUCCESS);
        }
        if (strcmp(buffer, "undo\n") == 0) {
            if (board_ext.past_plays_count >= 2 && board_ext.past_plays_count < MAX_GAME_PLAYS) {
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

void text_mode_play(int player_one_is_human, int player_two_is_human) {
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
            int valid_plays_i = enumerate_legal_plays(valid_plays, &board, 0, NULL);
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

void text_mode() {
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

void self_play() {
    printf("Prawn %s - self play mode\n\n", PROGRAM_VERSION);

    text_mode_play(0, 0);
}
