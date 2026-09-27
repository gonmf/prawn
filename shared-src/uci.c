#include "uci.h"

static FILE * uci_log = NULL;

int format_play_uci(char * dest, const play_t * play) {
    int i = sprintf(dest, "%c%d%c%d", 'a' + play->from_x, 8 - play->from_y, 'a' + play->to_x, 8 - play->to_y);

    switch (play->promotion_option) {
        case PROMOTION_QUEEN:  dest[i++] = 'q'; break;
        case PROMOTION_KNIGHT: dest[i++] = 'n'; break;
        case PROMOTION_BISHOP: dest[i++] = 'b'; break;
        case PROMOTION_ROOK:   dest[i++] = 'r'; break;
    }

    dest[i] = 0;
    return i;
}

void send_search_info(int depth, int score, const play_t * play) {
    if (uci_log == NULL) {
        return;
    }

    int value = score;
    char score_str[32];

    if (value > MATE_THRESHOLD || value < -MATE_THRESHOLD) {
        int plays = (MATE_SCORE - (value > 0 ? value : -value)) / 128 + 1;
        int moves = (plays + 1) / 2;
        sprintf(score_str, "mate %d", value > 0 ? moves : -moves);
    } else {
        sprintf(score_str, "cp %d", value);
    }

    char play_str[8];
    format_play_uci(play_str, play);

    long int elapsed = search_elapsed_ms();
    unsigned long long int nps = elapsed > 0 ? (search_nodes * 1000ULL) / (unsigned long long int)elapsed : 0ULL;

    char line[256];
    sprintf(line, "info depth %d score %s nodes %llu nps %llu time %ld pv %s",
        depth, score_str, (unsigned long long int)search_nodes, nps, elapsed, play_str);

    send_uci_command(uci_log, line);
}

void send_uci_command(FILE * fd, const char * str) {
    fprintf(fd, "< %s\n", str);
    fflush(fd);
    printf("%s\n", str);
    fflush(stdout);
}

FILE * init_log_file(const char * program_name) {
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
long int read_go_option(const char * cmd, const char * name) {
    char key[32];
    sprintf(key, " %s ", name);

    const char * at = strstr(cmd, key);
    if (at == NULL) {
        return -1;
    }

    return atol(at + strlen(key));
}

void set_search_limits(const char * cmd) {
    search_depth_limit = DEFAULT_SEARCH_DEPTH;
    search_budget_ms = 0;
    search_soft_ms = 0;

    long int depth = read_go_option(cmd, "depth");
    long int movetime = read_go_option(cmd, "movetime");
    long int my_time = read_go_option(cmd, board.color == WHITE_COLOR ? "wtime" : "btime");
    long int my_increment = read_go_option(cmd, board.color == WHITE_COLOR ? "winc" : "binc");
    long int movestogo = read_go_option(cmd, "movestogo");

    // "go infinite" is deliberately not honoured: without a "stop" command to end it
    if (depth > 0) {
        search_depth_limit = (int)MIN(depth, (long int)MAX_SEARCH_DEPTH);
    } else if (movetime > 0) {
        search_depth_limit = MAX_SEARCH_DEPTH;
        search_soft_ms = movetime;
        search_budget_ms = movetime;
    } else if (my_time > 0) {
        // we simply assume we have always 30 moves to as a way to smooth the time spent curve
        search_depth_limit = MAX_SEARCH_DEPTH;
        search_soft_ms = my_time / (movestogo > 0 ? movestogo : 30) + (my_increment > 0 ? my_increment * 3 / 4 : 0);

        // Only starting an iteration is held to the soft limit. Cutting one off part way throws
        // away everything it had done, which costs more than letting it run over. The room to run
        // over is given up when the clock is too short to afford it, leaving the two limits equal.
        long int overrun_cap = MAX(my_time / TIME_HARD_CLOCK_SHARE, search_soft_ms);
        search_budget_ms = MIN(search_soft_ms * TIME_HARD_MULTIPLIER, overrun_cap);
    }

    if (search_budget_ms != 0 && my_time > 0 && search_budget_ms > my_time - TIME_MOVE_OVERHEAD_MS) {
        long int my_time2 = my_time - TIME_MOVE_OVERHEAD_MS;
        search_budget_ms = MAX(my_time2, 10);
    }

    if (search_soft_ms > search_budget_ms) {
        search_soft_ms = search_budget_ms;
    }
}

void uci_mode(FILE * fd) {
    fprintf(fd, "# Starting in UCI mode.\n");
    fflush(fd);

    uci_log = fd;

    arbitrate_draws = extend_uci;

    while (1) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        char * newline = strchr(buffer, '\n');
        if (newline != NULL) {
            *newline = 0;
        } else if (!feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);

            fprintf(fd, "# Input line longer than %d, ignored\n", INPUT_BUFFER_SIZE - 1);
            fflush(fd);
            continue;
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
            hash_table_reset();
            uci_game_in_error_state = 0;
            continue;
        }
        if (strncmp(buffer, "position ", strlen("position ")) == 0) {
            char * str = strstr(buffer, " startpos");
            if (str != NULL && (str[9] == 0 || str[9] == ' ')) {
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
                        int valid_plays_i = enumerate_legal_plays(valid_plays, &board, 0, NULL);
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

            char play_str[8];
            format_play_uci(play_str, &play);
            sprintf(buffer, "bestmove %s", play_str);
            send_uci_command(fd, buffer);
            continue;
        }
    }

    if (fd != stderr) {
        fclose(fd);
    }
}
