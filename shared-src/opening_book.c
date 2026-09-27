#include "opening_book.h"

void init_opening_book() {
    opening_book_size = 0;

    play_t play;
    play.promotion_option = 0;
    play_t valid_plays[218];
    int valid_plays_i;
    int play_is_legal;
    int file_row = 0;

    play.promotion_option = 0;
    FILE * fp = open_data_file("openings.txt", "r");
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
        char * r = fgets(buffer, sizeof(buffer), fp);
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

            valid_plays_i = enumerate_legal_plays(valid_plays, &board, 0, NULL);
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

int get_opening_book_play(play_t * play, uint64_t board_hash) {
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
