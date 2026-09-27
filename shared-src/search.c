#include "search.h"

// Two quiet plays per ply that last caused a cut there, and a running count per origin and
// destination square. A play that refutes one line usually refutes its siblings.
static int16_t killers[MAX_TOTAL_SEARCH_DEPTH + 1][2];

static int history[2][64][64];

// Quiescence search. Reached from the frontier of the main search, it keeps resolving captures until
// the position is quiet, so that estimate_board_score is never taken in the middle of a trade.
// The side to move may always stop capturing and accept the static score; that score is the floor (for
// white) or the ceiling (for black) of the node. The one exception is being in check, where doing nothing
// is not legal: there every reply is searched, quiet ones included, and only the depth limit ends it.
int negamax_capture_only(const board_t * board, int depth, int max_depth, int alpha, int beta, int64_t hash) {
    if (search_aborted || out_of_time()) {
        search_aborted = 1;
        return NO_SCORE;
    }

    search_history[search_history_count + depth] = hash;

    if (is_repetition(hash, depth, board->halfmoves) || board->halfmoves >= 100 || insufficient_material(board)) {
        return DRAW_SCORE;
    }

    int white_to_play = board->color == WHITE_COLOR;
    int own_color = board->color;
    int opponent_color = white_to_play ? BLACK_COLOR : WHITE_COLOR;

    // Quiescence nodes look at captures only, so whatever they return is worth no full play of
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
        int score = white_to_play ? estimate_board_score(board) : -estimate_board_score(board);

        hash_table_insert(hash, pack_score(score, TYPE_EXACT), 0, 0);
        return score;
    }

    board_t board_cpy;
    // For why 218, see https://lichess.org/@/Tobs40/blog/why-a-position-cant-have-more-than-218-moves/a5xdxeqs
    play_t valid_plays[218];

    int in_check;
    int valid_plays_i = enumerate_legal_plays(valid_plays, board, 1, &in_check);

    // Only a list holding every evasion can tell a mate from a quiet position. Without check
    // this one holds captures alone, and having none of them means nothing to take, not stalemate.
    if (in_check && valid_plays_i == 0) {
        int score = -MATE_SCORE + depth * 128;

        // A finished game is worth the same however many plays were left to search, so this is
        // the one result that can be stored at the greatest draft there is.
        hash_table_insert(hash, pack_score(score_to_hash(score, depth), TYPE_EXACT), MAX_TOTAL_SEARCH_DEPTH, 0);
        return score;
    }

    // The play that came out best the last time this position was searched goes first. A quiet
    // one is simply not in this list unless the node is in check, and is then not looked for.
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
        best_score = white_to_play ? estimate_board_score(board) : -estimate_board_score(board);

        if (best_score >= beta) {
            hash_table_insert(hash, pack_score(best_score, TYPE_LOWER_BOUND), 0, 0);
            return best_score;
        }

        alpha = MAX(alpha, best_score);
    }

    int stand_pat = best_score;

    for (int i = 0; i < valid_plays_i; ++i) {
        if (!in_check) {
            int to_p = valid_plays[i].to_y * 8 + valid_plays[i].to_x;
            char victim = identify_piece_of(board, to_p, opponent_color);
            int victim_value = piece_value(victim);

            // never worth a sacrifice this much bellow alpha
            if (victim != ' ' && valid_plays[i].promotion_option == 0
                && stand_pat + victim_value + DELTA_MARGIN < alpha) {
                continue;
            }

            char attacker = identify_piece_of(board, valid_plays[i].from_y * 8 + valid_plays[i].from_x, own_color);
            if (victim_value < piece_value(attacker) && static_exchange_eval(board, &valid_plays[i], white_to_play) < 0) {
                continue;
            }
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        if (white_to_play) {
            just_play_white_complex(&board_cpy, &valid_plays[i], &this_hash);
        } else {
            just_play_black_complex(&board_cpy, &valid_plays[i], &this_hash);
        }

        int child = negamax_capture_only(&board_cpy, depth + 1, max_depth, -beta, -alpha, this_hash);
        int score = child == NO_SCORE ? NO_SCORE : -child;

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

void record_quiet_cutoff(int color_index, int depth, const play_t * play, int draft) {
    int16_t packed = pack_play(play);
    if (killers[depth][0] != packed) {
        killers[depth][1] = killers[depth][0];
        killers[depth][0] = packed;
    }

    int from = play->from_y * 8 + play->from_x;
    int to = play->to_y * 8 + play->to_x;
    history[color_index][from][to] += draft * draft;

    if (history[color_index][from][to] > HISTORY_MAX) {
        for (int a = 0; a < 64; ++a) {
            for (int b = 0; b < 64; ++b) {
                history[color_index][a][b] /= 2;
            }
        }
    }
}

int negamax(
    const board_t * board,
    int depth,
    int max_depth,
    int alpha,
    int beta,
    int64_t hash,
    int can_null_prune
) {
    if (search_aborted || out_of_time()) {
        search_aborted = 1;
        return NO_SCORE;
    }

    search_history[search_history_count + depth] = hash;

    if (is_repetition(hash, depth, board->halfmoves) || board->halfmoves >= 100 || insufficient_material(board)) {
        return DRAW_SCORE;
    }

    int white_to_play = board->color == WHITE_COLOR;
    int opponent_color = white_to_play ? BLACK_COLOR : WHITE_COLOR;
    int color_index = white_to_play ? 0 : 1;

    // How many plays this node still has to search below it, which is what its score is worth.
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
        int score = white_to_play ? estimate_board_score(board) : -estimate_board_score(board);

        hash_table_insert(hash, pack_score(score, TYPE_EXACT), 0, 0);
        return score;
    }

    board_t board_cpy;
    // For why 218, see https://lichess.org/@/Tobs40/blog/why-a-position-cant-have-more-than-218-moves/a5xdxeqs
    play_t valid_plays[218];
    char captures[218];

    int in_check;
    int valid_plays_i = enumerate_legal_plays(valid_plays, board, 0, &in_check);
    if (valid_plays_i == 0) {
        int score = in_check ? -MATE_SCORE + depth * 128 : DRAW_SCORE;

        // A finished game is worth the same however many plays were left to search, so this is
        // the one result that can be stored at the greatest draft there is.
        hash_table_insert(hash, pack_score(score_to_hash(score, depth), TYPE_EXACT), MAX_TOTAL_SEARCH_DEPTH, 0);
        return score;
    }

    // extend very narrow searches of when in check
    if (in_check && max_depth < MAX_SEARCH_DEPTH) {
        max_depth++;
        draft++;
    }

    if (can_null_prune && !in_check && draft >= 3 && beta < MATE_THRESHOLD && (white_to_play
            ? (board->white_knights | board->white_bishops | board->white_rooks | board->white_queens)
            : (board->black_knights | board->black_bishops | board->black_rooks | board->black_queens))) {
        board_t null_board;
        memcpy(&null_board, board, sizeof(board_t));
        null_board.color = opponent_color;
        null_board.en_passant_x = NO_EN_PASSANT;

        int64_t null_hash = hash ^ zobrist_side_to_move;
        if (board->en_passant_x != NO_EN_PASSANT) {
            null_hash ^= zobrist_en_passant[(int)(board->en_passant_x)];
        }

        int null_max_depth = max_depth - NULL_MOVE_REDUCTION;
        int child;
        if (depth + 1 == null_max_depth) {
            child = negamax_capture_only(&null_board, depth + 1, null_max_depth + QUIESCENCE_EXTRA_DEPTH, -beta, -beta + 1, null_hash);
        } else {
            child = negamax(&null_board, depth + 1, null_max_depth, -beta, -beta + 1, null_hash, 0);
        }

        if (child != NO_SCORE && -child >= beta) {
            return -child;
        }
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
        captures[i] = identify_piece_of(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x, opponent_color) != ' ';
        if (i == 0 && tt_play_first) {
            // Taken here whether it is a capture or not, and marked so the quiet pass skips it.
            captures[0] = 1;
        } else if (!captures[i]) {
            continue;
        }

        memcpy(&board_cpy, board, sizeof(board_t));
        int64_t this_hash = hash;

        if (white_to_play) {
            just_play_white_complex(&board_cpy, &valid_plays[i], &this_hash);
        } else {
            just_play_black_complex(&board_cpy, &valid_plays[i], &this_hash);
        }

        int child;
        if (depth + 1 == max_depth) {
            child = negamax_capture_only(&board_cpy, depth + 1, max_depth + QUIESCENCE_EXTRA_DEPTH, -beta, -alpha, this_hash);
        } else {
            child = negamax(&board_cpy, depth + 1, max_depth, -beta, -alpha, this_hash, 1);
        }
        int score = child == NO_SCORE ? NO_SCORE : -child;

        if (score != NO_SCORE) {
            if (best_score == NO_SCORE || score > best_score) {
                best_score = score;
                best_play = pack_play(&valid_plays[i]);
            }
            if (score >= beta) {
                if (identify_piece_of(board, valid_plays[i].to_y * 8 + valid_plays[i].to_x, opponent_color) == ' ') {
                    record_quiet_cutoff(color_index, depth, &valid_plays[i], draft);
                }
                breakfor = 1;
                break;
            }
            alpha = MAX(alpha, score);
        }
    }

    if (!breakfor) {
        int quiet_score[218];
        for (int i = 0; i < valid_plays_i; ++i) {
            if (captures[i]) {
                continue;
            }

            int16_t packed = pack_play(&valid_plays[i]);
            if (packed == killers[depth][0]) {
                quiet_score[i] = KILLER_FIRST;
            } else if (packed == killers[depth][1]) {
                quiet_score[i] = KILLER_SECOND;
            } else {
                quiet_score[i] = history[color_index][valid_plays[i].from_y * 8 + valid_plays[i].from_x][valid_plays[i].to_y * 8 + valid_plays[i].to_x];
            }
        }

        int quiets_tried = 0; // for LMR

        while (1) {
            // Picked as the loop reaches it rather than sorted up front, so a cut on the first
            // one costs a single pass. captures[] marks a play as taken.
            int i = -1;
            for (int j = 0; j < valid_plays_i; ++j) {
                if (captures[j]) {
                    continue;
                }
                if (i == -1 || quiet_score[j] > quiet_score[i]) {
                    i = j;
                }
            }
            if (i == -1) {
                break;
            }
            captures[i] = 1;
            quiets_tried++;

            memcpy(&board_cpy, board, sizeof(board_t));
            int64_t this_hash = hash;

            if (white_to_play) {
                just_play_white_complex(&board_cpy, &valid_plays[i], &this_hash);
            } else {
                just_play_black_complex(&board_cpy, &valid_plays[i], &this_hash);
            }

            // Ordering has already put the plays worth searching first, so the ones this far down
            // the list are searched short and only at a window wide enough to tell whether they
            // beat alpha. One that does is not trusted: it is searched again at full depth.
            int depth_subtracted = 0;
            if (!in_check && draft >= 3 && quiets_tried > LMR_MIN_PLAYS) {
                depth_subtracted = quiets_tried > LMR_MIN_PLAYS + 4 ? 2 : 1;
                if (depth_subtracted > draft - 1) {
                    depth_subtracted = draft - 1;
                }
            }

            int child;
            if (depth_subtracted > 0) {
                int reduced_max = max_depth - depth_subtracted;
                if (depth + 1 == reduced_max) {
                    child = negamax_capture_only(&board_cpy, depth + 1, reduced_max + QUIESCENCE_EXTRA_DEPTH, -alpha - 1, -alpha, this_hash);
                } else {
                    child = negamax(&board_cpy, depth + 1, reduced_max, -alpha - 1, -alpha, this_hash, 1);
                }

                if (child != NO_SCORE && -child > alpha) {
                    depth_subtracted = 0;
                }
            }

            // A quiet move at the frontier hands off to the quiescence search exactly like a
            // capture does: it can just as easily leave a piece hanging.
            if (depth_subtracted == 0) {
                if (depth + 1 == max_depth) {
                    child = negamax_capture_only(&board_cpy, depth + 1, max_depth + QUIESCENCE_EXTRA_DEPTH, -beta, -alpha, this_hash);
                } else {
                    child = negamax(&board_cpy, depth + 1, max_depth, -beta, -alpha, this_hash, 1);
                }
            }

            int score = child == NO_SCORE ? NO_SCORE : -child;

            if (score != NO_SCORE) {
                if (best_score == NO_SCORE || score > best_score) {
                    best_score = score;
                    best_play = pack_play(&valid_plays[i]);
                }
                if (score >= beta) {
                    record_quiet_cutoff(color_index, depth, &valid_plays[i], draft);
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

long int predicted_iteration_ms(const long int * iter_ms, int depth) {
    long int ratio = TIME_RATIO_PRIOR;

    // Odd and even iterations do not cost the same, the side to move at the leaf alternating
    // between them, so what the next one will cost is read from two back and not from the last.
    if (depth >= 3 && iter_ms[depth - 2] >= 20) {
        ratio = iter_ms[depth - 1] * 100 / iter_ms[depth - 2];
        ratio = MIN(MAX(ratio, (long int)TIME_RATIO_MIN), (long int)TIME_RATIO_MAX);
        ratio = (ratio * 7 + TIME_RATIO_PRIOR * 3) / 10;
    }

    return iter_ms[depth] * ratio / 100;
}

long int time_allowance(int changed, int score_drop) {
    long int factor = 100;

    if (changed) {
        factor += TIME_UNSTABLE_EXTRA;
    }
    if (score_drop > TIME_DROP_THRESHOLD) {
        factor += MIN(score_drop, 400) / 8;
    }

    return MIN(search_soft_ms * factor / 100, search_budget_ms);
}

int ai_play(play_t * play) {
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

    int valid_plays_i = enumerate_legal_plays(valid_plays, &board, 0, NULL);
    if (valid_plays_i == 0) {
        if (king_threatened(&board)) {
            return CHECK_MATE;
        } else {
            return DRAW;
        }
    }

    if (valid_plays_i == 1) {
        actual_play(&board, &board_ext, &valid_plays[0]);
        *play = valid_plays[0];
        return 1;
    }

    ++hash_table_age;

    int64_t root_hash = hash_from_board(&board);

    search_history_count = 0;
    for (unsigned int i = 0; i < board_ext.past_plays_count; ++i) {
        search_history[search_history_count++] = board_ext.past_hashes[i];
    }
    search_history[search_history_count++] = root_hash;

    memset(killers, 0, sizeof(killers));
    for (int c = 0; c < 2; ++c) {
        for (int a = 0; a < 64; ++a) {
            for (int b = 0; b < 64; ++b) {
                history[c][a][b] /= 2;
            }
        }
    }

    gettimeofday(&search_start, NULL);
    search_aborted = 0;
    search_nodes = 0;

    long int iter_ms[MAX_SEARCH_DEPTH + 1];
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

            int child = negamax(&board_cpy, 0, max_depth, -beta, -alpha, child_hash, 1);
            int score = child == NO_SCORE ? NO_SCORE : -child;

            if (search_aborted) {
                break;
            }

            if (score != NO_SCORE) {
                if (iter_score == NO_SCORE || score > iter_score) {
                    iter_score = score;
                    iter_play = i;
                }
                alpha = MAX(alpha, score);
            }
        }

        // An iteration that ran out of time part way through has only seen some of the plays, so
        // the one it likes best means nothing. The previous iteration's answer stands.
        if (search_aborted || iter_score == NO_SCORE) {
            break;
        }

        int changed = iter_play != 0;
        int score_drop = best_score == NO_SCORE ? 0 : best_score - iter_score;

        best_score = iter_score;
        iter_ms[max_depth] = search_elapsed_ms();

        // We lead the next iteration with this one's best play for the most gain of searching by
        // increasing depth comes from.
        if (iter_play != 0) {
            play_t tmp_play = valid_plays[0];
            valid_plays[0] = valid_plays[iter_play];
            valid_plays[iter_play] = tmp_play;
        }

        send_search_info(max_depth, best_score, &valid_plays[0]);

        if (best_score >= MATE_THRESHOLD || best_score <= -MATE_THRESHOLD) {
            break;
        }

        // Starting an iteration there is no chance of finishing spends the rest of the budget on a
        // result that gets thrown away.
        if (search_soft_ms != 0 && predicted_iteration_ms(iter_ms, max_depth)
                > time_allowance(changed, score_drop)) {
            break;
        }
    }

    // The first iteration is never abandoned part way, so valid_plays[0] is always the best
    // play of the deepest iteration that finished.
    actual_play(&board, &board_ext, &valid_plays[0]);
    *play = valid_plays[0];
    return 1;
}
