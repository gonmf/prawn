#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void read_line(char * dest, int fd) {
    int r = 0;
    while (1) {
        int ir = read(fd, dest + r, 1024 - r);
        if (ir == -1) {
            fprintf(stderr, "error\n");
            exit(EXIT_FAILURE);
        }

        r += ir;

        if (dest[r - 1] == '\n') {
            dest[r] = 0;
            return;
        }
    }
}

static void write_line(int fd, const char * s) {
    write(fd, s, strlen(s));
}

static long elapsed_ms(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000L +
           (end.tv_usec - start.tv_usec) / 1000L;
}

int main(int argc, char * argv[]) {
    if (argc != 3 && argc != 4) {
        printf("Illegal input. Example:\n");
        printf("%s \"prawn\" \"prawn\" [--collection=bait.input]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int random_play = 0;
    char * start_positions[400];
    int start_positions_count = 0;
    if (argc == 3) {
        start_positions[0] = malloc(100);
        sprintf(start_positions[0], "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        start_positions_count = 1;
        random_play = 1;
    } else {
        FILE * fp = fopen("bait.input", "r");

        while (1) {
            start_positions[start_positions_count] = malloc(100);

            char * r = fgets(start_positions[start_positions_count], 100, fp);
            if (r == NULL || strlen(r) < 1) {
                free(start_positions[start_positions_count]);
                break;
            }
            r[strlen(r) - 1] = 0;

            start_positions_count++;
        }

        fclose(fp);

        if (start_positions_count == 0) {
            fprintf(stderr, "No starting position loaded\n");
            return EXIT_FAILURE;
        }
    }

    if (!random_play) {
        printf("Loaded %d starting positions\n", start_positions_count);
    }

    printf("PROGRAM 1 = %s\n", argv[1]);
    printf("PROGRAM 2 = %s\n", argv[2]);

    char buffer[1024];
    char buffer2[1024];

    int prog1_inPipe[2], prog1_outPipe[2];
    if (pipe(prog1_inPipe) == -1 || pipe(prog1_outPipe) == -1) {
        perror("pipe");
        exit(1);
    }

    if (fork() == 0) {
        close(prog1_inPipe[1]);
        close(prog1_outPipe[0]);

        dup2(prog1_inPipe[0], STDIN_FILENO);
        dup2(prog1_outPipe[1], STDOUT_FILENO);

        close(prog1_inPipe[0]);
        close(prog1_outPipe[1]);

        execlp(argv[1], argv[1], "--extend-uci", random_play ? NULL : "--no-book", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }

    int prog2_inPipe[2], prog2_outPipe[2];
    if (pipe(prog2_inPipe) == -1 || pipe(prog2_outPipe) == -1) {
        perror("pipe");
        exit(1);
    }

    if (fork() == 0) {
        close(prog2_inPipe[1]);
        close(prog2_outPipe[0]);

        dup2(prog2_inPipe[0], STDIN_FILENO);
        dup2(prog2_outPipe[1], STDOUT_FILENO);

        close(prog2_inPipe[0]);
        close(prog2_outPipe[1]);

        execlp(argv[2], argv[2], "--extend-uci", random_play ? NULL : "--no-book", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }

    close(prog1_inPipe[0]);
    close(prog1_outPipe[1]);
    close(prog2_inPipe[0]);
    close(prog2_outPipe[1]);

    int player1_wins = 0;
    int total_draws = 0;
    int white_wins = 0;
    int total_error = 0;

    struct timeval start, end;
    int round = 0;
    double total_elapsed = 0;

    printf("\rRounds #\t|\tP1 WR\t|\tWH WR\t|\tDRAWS\t|\tERROR\t|\tavg time (s)\n");

    int start_position = start_positions_count - 1;
    int max_rounds = random_play ? -1 : start_positions_count * 2;

    while (max_rounds == -1 || round < max_rounds) {
        round += 1;
        if (round & 1) {
            start_position++;
        }
        int start_pos = start_position % start_positions_count;

        gettimeofday(&start, NULL);

        write_line(prog1_inPipe[1], "ucinewgame\n");
        write_line(prog2_inPipe[1], "ucinewgame\n");
        sprintf(buffer, "position fen %s\n", start_positions[start_pos]);
        write_line(prog1_inPipe[1], buffer);
        write_line(prog2_inPipe[1], buffer);

        int turn = 0;
        char * move;

        while (turn < 256) {
            turn++;
            printf("\rTurn %d...", turn);
            fflush(stdout);

            int white_is_player_1 = round & 1;

            if (turn > 1 || white_is_player_1) {
                write_line(prog1_inPipe[1], "go\n");

                read_line(buffer, prog1_outPipe[0]);

                if (strcmp(buffer, "error\n") == 0) {
                    total_error++;
                    break;
                }
                if (strcmp(buffer, "loss\n") == 0) {
                    if (!white_is_player_1) {
                        white_wins++;
                    }
                    break;
                }
                if (strcmp(buffer, "draw\n") == 0) {
                    total_draws++;
                    break;
                }

                move = buffer + strlen("bestmove ");
                if (strlen(move) < 5 || strlen(move) > 6) {
                    total_error++;
                    break;
                }
                sprintf(buffer2, "position moves %s", move);
                write_line(prog2_inPipe[1], buffer2);
            }

            write_line(prog2_inPipe[1], "go\n");

            read_line(buffer, prog2_outPipe[0]);

            if (strcmp(buffer, "error\n") == 0) {
                total_error++;
                break;
            }
            if (strcmp(buffer, "loss\n") == 0) {
                player1_wins++;
                if (white_is_player_1) {
                    white_wins++;
                }
                break;
            }
            if (strcmp(buffer, "draw\n") == 0) {
                total_draws++;
                break;
            }

            move = buffer + strlen("bestmove ");
            if (strlen(move) < 5 || strlen(move) > 6) {
                total_error++;
                break;
            }
            sprintf(buffer2, "position moves %s", move);
            write_line(prog1_inPipe[1], buffer2);
        }

        if (turn == 256) {
            total_error++;
        }

        write_line(prog1_inPipe[1], "log_fen\n");
        write_line(prog2_inPipe[1], "log_fen\n");

        gettimeofday(&end, NULL);

        float total = round / 100.0;
        total_elapsed += ((double)elapsed_ms(start, end)) / 1000.0;
        printf("\r             ");
        fflush(stdout);

        int total_finished_games = round - total_draws - total_error;
        double player1_wr = total_finished_games ? player1_wins / ((double)total_finished_games) * 100.0 : 0.0;
        double white_wr = total_finished_games ? white_wins / ((double)total_finished_games) * 100.0 : 0.0;
        printf("\rRounds %d\t|\t%.2f%%\t|\t%.2f%%\t|\t%.2f%%\t|\t%.2f%%\t|\t%.3f\n", round, player1_wr, white_wr, total_draws / total, total_error / total, total_elapsed / round);
    }

    return EXIT_SUCCESS;
}
