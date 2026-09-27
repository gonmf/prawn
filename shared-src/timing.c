#include "common.h"

long int elapsed_ms(struct timeval start, struct timeval end) {
    return (end.tv_sec - start.tv_sec) * 1000L + (end.tv_usec - start.tv_usec) / 1000L;
}

long int search_elapsed_ms() {
    struct timeval now;
    gettimeofday(&now, NULL);

    return elapsed_ms(search_start, now);
}

// A position already seen is scored as a draw on its first repetition rather than its third: a side
// able to repeat once can nearly always repeat again, and waiting for the third costs time to see.
// Only positions since the last pawn play or capture can repeat, which bounds the search.
int is_repetition(int64_t hash, int depth, int halfmoves) {
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

int out_of_time() {
    // avoid calling gettimeofday at every node
    if ((++search_nodes & 4095) != 0 || search_budget_ms == 0 || !search_abortable) {
        return 0;
    }

    return search_elapsed_ms() >= search_budget_ms;
}
