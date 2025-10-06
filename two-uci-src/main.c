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
    if (argc != 3) {
        printf("Illegal input. Example:\n");
        printf("%s \"prawn\" \"prawn\"\n", argv[0]);
        return EXIT_FAILURE;
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

        execlp(argv[1], argv[1], "--extend-uci", NULL);
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

        execlp(argv[2], argv[2], "--extend-uci", NULL);
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

    while (round < 10) {
        round += 1;

        gettimeofday(&start, NULL);

        write_line(prog1_inPipe[1], "ucinewgame\n");
        write_line(prog2_inPipe[1], "ucinewgame\n");

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

                if (strcmp(buffer, "loss\n") == 0) {
                    if (!white_is_player_1) {
                        white_wins++;
                    }
                    write_line(prog1_inPipe[1], "log_fen\n");
                    break;
                }
                if (strcmp(buffer, "draw\n") == 0) {
                    total_draws++;
                    write_line(prog1_inPipe[1], "log_fen\n");
                    break;
                }

                move = buffer + strlen("bestmove ");
                sprintf(buffer2, "position moves %s", move);
                write_line(prog2_inPipe[1], buffer2);
            }

            write_line(prog2_inPipe[1], "go\n");

            read_line(buffer, prog2_outPipe[0]);

            if (strcmp(buffer, "loss\n") == 0) {
                player1_wins++;
                if (white_is_player_1) {
                    white_wins++;
                }
                write_line(prog2_inPipe[1], "log_fen\n");
                break;
            }
            if (strcmp(buffer, "draw\n") == 0) {
                total_draws++;
                write_line(prog2_inPipe[1], "log_fen\n");
                break;
            }

            move = buffer + strlen("bestmove ");
            sprintf(buffer2, "position moves %s", move);
            write_line(prog1_inPipe[1], buffer2);
        }

        if (turn == 256) {
            total_error++;
        }

        gettimeofday(&end, NULL);

        float total = round / 100.0;
        total_elapsed += ((double)elapsed_ms(start, end)) / 1000.0;
        printf("\r             ");
        fflush(stdout);
        printf("\rRounds %d\t|\t%.2f%%\t|\t%.2f%%\t|\t%.2f%%\t|\t%.2f%%\t|\t%.3f\n", round, player1_wins / total, total_draws / total, white_wins / total, total_error / total, total_elapsed / round);
    }

    return EXIT_SUCCESS;
}
