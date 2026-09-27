#include "evaluation.h"

typedef struct {
    uint64_t white_pawns;
    uint64_t black_pawns;
    uint64_t white_passed;
    uint64_t black_passed;
    uint64_t white_attacks;
    uint64_t black_attacks;
    int32_t score;
} pawn_hash_entry_t;

static uint64_t white_passed_masks[64];

static uint64_t black_passed_masks[64];

static uint64_t white_front_masks[64];

static uint64_t black_front_masks[64];

static uint64_t white_behind_masks[64];

static uint64_t black_behind_masks[64];

static uint64_t file_masks[8];

static const int pawn_pst[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

static pawn_hash_entry_t pawn_hash_table[PAWN_HASH_SIZE];

void populate_passed_pawn_masks() {
    for (int p = 0; p < 64; ++p) {
        white_passed_masks[p] = 0ULL;
        black_passed_masks[p] = 0ULL;
        white_front_masks[p] = 0ULL;
        black_front_masks[p] = 0ULL;

        int x = p % 8;
        int y = p / 8;

        for (int x2 = x - 1; x2 <= x + 1; ++x2) {
            if (x2 < 0 || x2 > 7) {
                continue;
            }

            for (int y2 = 0; y2 < y; ++y2) {
                white_passed_masks[p] |= 1ULL << (y2 * 8 + x2);
                if (x2 == x) {
                    white_front_masks[p] |= 1ULL << (y2 * 8 + x2);
                }
            }
            for (int y2 = y + 1; y2 < 8; ++y2) {
                black_passed_masks[p] |= 1ULL << (y2 * 8 + x2);
                if (x2 == x) {
                    black_front_masks[p] |= 1ULL << (y2 * 8 + x2);
                }
            }

            if (x2 != x) {
                for (int y2 = y; y2 < 8; ++y2) {
                    white_behind_masks[p] |= 1ULL << (y2 * 8 + x2);
                }
                for (int y2 = y; y2 >= 0; --y2) {
                    black_behind_masks[p] |= 1ULL << (y2 * 8 + x2);
                }
            }
        }
    }

    for (int x = 0; x < 8; ++x) {
        file_masks[x] = 0ULL;
        for (int y = 0; y < 8; ++y) {
            file_masks[x] |= 1ULL << (y * 8 + x);
        }
    }
}

static int cannot_force_mate(int pawns, int knights, int bishops, int rooks, int queens) {
    if (pawns || rooks || queens) {
        return 0;
    }
    if (knights + bishops <= 1) {
        return 1;
    }
    return bishops == 0 && knights == 2;
}

static int distance_from_centre(int sq) {
    int x = sq % 8;
    int y = sq / 8;
    int dx = x < 4 ? 3 - x : x - 4;
    int dy = y < 4 ? 3 - y : y - 4;

    return dx + dy;
}

static int king_distance(int a, int b) {
    int dx = (a % 8) - (b % 8);
    int dy = (a / 8) - (b / 8);
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    return dx > dy ? dx : dy;
}

static int passed_pawn_bonus(
    const board_t * board,
    int sq,
    int white,
    int midgame_weight,
    int endgame_weight, int weight_span
) {
    static const int passed_midgame[8] = { 0,  5, 10, 20, 35,  60, 100, 0 };
    static const int passed_endgame[8] = { 0, 10, 20, 40, 70, 120, 200, 0 };

    int x = sq % 8;
    int y = sq / 8;
    int rank = white ? 7 - y : y;

    int bonus = (passed_midgame[rank] * midgame_weight + passed_endgame[rank] * endgame_weight) / weight_span;

    uint64_t occupied = board->white_mask | board->black_mask;
    uint64_t own_pawns = white ? board->white_pawns : board->black_pawns;

    int ahead = white ? sq - 8 : sq + 8;
    if (occupied & (1ULL << ahead)) {
        bonus /= PASSED_BLOCKED_DIV;
    }

    if (own_pawns & (white ? black_pawn_capture_masks[sq] : white_pawn_capture_masks[sq])) {
        bonus += PASSED_PROTECTED;
    }

    uint64_t beside = 0ULL;
    if (x > 0) {
        beside |= 1ULL << (y * 8 + x - 1);
    }
    if (x < 7) {
        beside |= 1ULL << (y * 8 + x + 1);
    }
    if (own_pawns & beside) {
        bonus += PASSED_PHALANX;
    }

    uint64_t front = white ? white_front_masks[sq] : black_front_masks[sq];
    uint64_t opponent_kings = white ? board->black_kings : board->white_kings;

    if (opponent_kings) {
        if (opponent_kings & front) {
            bonus /= PASSED_KING_HELD_DIV;
        }

        int stop_sq = white ? sq - 8 : sq + 8;
        bonus += (king_distance(__builtin_ctzll(opponent_kings), stop_sq) - 3) * PASSED_KING_DISTANCE
            * endgame_weight / weight_span;
    }

    // pawn vs kind race
    uint64_t opponent_pieces = white
        ? (board->black_knights | board->black_bishops | board->black_rooks | board->black_queens)
        : (board->white_knights | board->white_bishops | board->white_rooks | board->white_queens);

    if (opponent_pieces == 0ULL && opponent_kings != 0ULL && (occupied & front) == 0ULL) {
        int steps = white ? y : 7 - y;
        if (steps == 6) {
            steps = 5;
        }

        int promotion_sq = white ? x : 56 + x;
        int own_turn = (board->color == WHITE_COLOR) == (white != 0);

        if (steps + (own_turn ? 0 : 1) < king_distance(__builtin_ctzll(opponent_kings), promotion_sq)) {
            bonus += PASSED_UNSTOPPABLE;
        }
    }

    return bonus;
}

static uint64_t pawn_attacks_of(uint64_t pawns, int white) {
    if (white) {
        return ((pawns & ~file_masks[0]) >> 9) | ((pawns & ~file_masks[7]) >> 7);
    }

    return ((pawns & ~file_masks[0]) << 7) | ((pawns & ~file_masks[7]) << 9);
}

static int pawn_structure_penalty(const board_t * board, int sq, int white) {
    int x = sq % 8;
    int penalty = 0;

    uint64_t own_pawns = white ? board->white_pawns : board->black_pawns;
    uint64_t enemy_pawns = white ? board->black_pawns : board->white_pawns;

    // counted from the rear pawn only, so a file of two is charged once and a file of three twice
    if (own_pawns & (white ? white_front_masks[sq] : black_front_masks[sq])) {
        penalty += DOUBLED_PAWN;
    }

    uint64_t beside_files = 0ULL;
    if (x > 0) {
        beside_files |= file_masks[x - 1];
    }
    if (x < 7) {
        beside_files |= file_masks[x + 1];
    }

    if ((own_pawns & beside_files) == 0ULL) {
        penalty += ISOLATED_PAWN;
    } else if ((own_pawns & (white ? white_behind_masks[sq] : black_behind_masks[sq])) == 0ULL) {
        int stop_sq = white ? sq - 8 : sq + 8;
        uint64_t attackers = white ? white_pawn_capture_masks[stop_sq] : black_pawn_capture_masks[stop_sq];

        if (attackers & enemy_pawns) {
            penalty += BACKWARD_PAWN;
        }
    }

    return penalty;
}

static const pawn_hash_entry_t * cached_pawn_evaluation(const board_t * board) {
    uint64_t wp = board->white_pawns;
    uint64_t bp = board->black_pawns;

    uint64_t key = wp * 0x9E3779B97F4A7C15ULL ^ (bp * 0xC2B2AE3D27D4EB4FULL);
    key ^= key >> 29;
    key *= 0xBF58476D1CE4E5B9ULL;
    key ^= key >> 32;

    pawn_hash_entry_t * entry = &pawn_hash_table[key & (PAWN_HASH_SIZE - 1)];
    if (entry->white_pawns == wp && entry->black_pawns == bp) {
        return entry;
    }

    int score = 0;
    uint64_t white_passers = 0ULL;
    uint64_t black_passers = 0ULL;

    uint64_t pawns = wp;
    while (pawns) {
        int sq = __builtin_ctzll(pawns);
        score += PAWN_VALUE + pawn_pst[sq] - pawn_structure_penalty(board, sq, 1);
        if ((white_passed_masks[sq] & bp) == 0ULL) {
            white_passers |= 1ULL << sq;
        }
        pawns &= pawns - 1;
    }

    pawns = bp;
    while (pawns) {
        int sq = __builtin_ctzll(pawns);
        score -= PAWN_VALUE + pawn_pst[sq ^ 56] - pawn_structure_penalty(board, sq, 0);
        if ((black_passed_masks[sq] & wp) == 0ULL) {
            black_passers |= 1ULL << sq;
        }
        pawns &= pawns - 1;
    }

    entry->white_pawns = wp;
    entry->black_pawns = bp;
    entry->white_passed = white_passers;
    entry->black_passed = black_passers;
    entry->white_attacks = pawn_attacks_of(wp, 1);
    entry->black_attacks = pawn_attacks_of(bp, 0);
    entry->score = score;

    return entry;
}

static uint64_t slider_attacks(uint64_t occupied, int sq, int first_dir, int last_dir) {
    static const int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
    };

    uint64_t attacks = 0ULL;
    int from_x = sq % 8;
    int from_y = sq / 8;

    for (int d = first_dir; d <= last_dir; ++d) {
        int dx = directions[d][0];
        int dy = directions[d][1];
        int x = from_x + dx;
        int y = from_y + dy;

        while (x >= 0 && x < 8 && y >= 0 && y < 8) {
            uint64_t to_mask = 1ULL << (y * 8 + x);
            attacks |= to_mask;

            if (occupied & to_mask) {
                break;
            }

            x += dx;
            y += dy;
        }
    }

    return attacks;
}

static int piece_mobility(
    const board_t * board,
    const pawn_hash_entry_t * pawn,
    int white,
    int midgame_weight,
    int endgame_weight,
    int weight_span
) {
    uint64_t occupied = board->white_mask | board->black_mask;
    uint64_t own = white ? board->white_mask : board->black_mask;
    uint64_t safe = ~own & ~(white ? pawn->black_attacks : pawn->white_attacks);

    uint64_t enemy_kings = white ? board->black_kings : board->white_kings;
    uint64_t king_zone = 0ULL;
    if (enemy_kings) {
        int enemy_king = __builtin_ctzll(enemy_kings);
        king_zone = king_moves_masks[enemy_king] | enemy_kings;
    }

    int mobility_mg = 0;
    int mobility_eg = 0;
    int danger = 0;

    uint64_t pieces = white ? board->white_knights : board->black_knights;
    while (pieces) {
        uint64_t attacks = knight_moves_masks[__builtin_ctzll(pieces)];
        int room = __builtin_popcountll(attacks & safe) - MOBILITY_KNIGHT_BASE;
        mobility_mg += room * MOBILITY_KNIGHT_MG;
        mobility_eg += room * MOBILITY_KNIGHT_EG;
        if (attacks & king_zone) {
            danger += KING_ATTACK_KNIGHT;
        }
        pieces &= pieces - 1;
    }

    pieces = white ? board->white_bishops : board->black_bishops;
    while (pieces) {
        uint64_t attacks = slider_attacks(occupied, __builtin_ctzll(pieces), 4, 7);
        int room = __builtin_popcountll(attacks & safe) - MOBILITY_BISHOP_BASE;
        mobility_mg += room * MOBILITY_BISHOP_MG;
        mobility_eg += room * MOBILITY_BISHOP_EG;
        if (attacks & king_zone) {
            danger += KING_ATTACK_BISHOP;
        }
        pieces &= pieces - 1;
    }

    pieces = white ? board->white_rooks : board->black_rooks;
    while (pieces) {
        uint64_t attacks = slider_attacks(occupied, __builtin_ctzll(pieces), 0, 3);
        int room = __builtin_popcountll(attacks & safe) - MOBILITY_ROOK_BASE;
        mobility_mg += room * MOBILITY_ROOK_MG;
        mobility_eg += room * MOBILITY_ROOK_EG;
        if (attacks & king_zone) {
            danger += KING_ATTACK_ROOK;
        }
        pieces &= pieces - 1;
    }

    pieces = white ? board->white_queens : board->black_queens;
    while (pieces) {
        uint64_t attacks = slider_attacks(occupied, __builtin_ctzll(pieces), 0, 7);
        int room = __builtin_popcountll(attacks & safe) - MOBILITY_QUEEN_BASE;
        mobility_mg += room * MOBILITY_QUEEN_MG;
        mobility_eg += room * MOBILITY_QUEEN_EG;
        if (attacks & king_zone) {
            danger += KING_ATTACK_QUEEN;
        }
        pieces &= pieces - 1;
    }

    int score = (mobility_mg * midgame_weight + mobility_eg * endgame_weight) / weight_span;

    int threat = MIN(danger * danger / KING_DANGER_DIV, KING_DANGER_MAX);
    score += threat * midgame_weight / weight_span;

    uint64_t own_kings = white ? board->white_kings : board->black_kings;
    uint64_t own_pawns = white ? board->white_pawns : board->black_pawns;

    if (own_kings) {
        int own_king = __builtin_ctzll(own_kings);
        int king_x = own_king % 8;
        int king_y = own_king / 8;
        int shelter = 0;

        for (int x = MAX(king_x - 1, 0); x <= MIN(king_x + 1, 7); ++x) {
            uint64_t ahead = own_pawns & (white
                ? white_front_masks[king_y * 8 + x]
                : black_front_masks[king_y * 8 + x]);

            if (ahead == 0ULL) {
                shelter += KING_OPEN_FILE;
            } else {
                // The one closest to the king is the one that shelters it.
                int nearest = white ? 63 - __builtin_clzll(ahead) : __builtin_ctzll(ahead);
                int ranks_away = white ? king_y - nearest / 8 : nearest / 8 - king_y;
                shelter += (ranks_away - 1) * KING_SHIELD_ADVANCED;
            }
        }

        score -= shelter * midgame_weight / weight_span;
    }

    return score;
}

int estimate_board_score(const board_t * board) {
    int white_pawns_n = __builtin_popcountll(board->white_pawns);
    int white_knights_n = __builtin_popcountll(board->white_knights);
    int white_bishops_n = __builtin_popcountll(board->white_bishops);
    int white_rooks_n = __builtin_popcountll(board->white_rooks);
    int white_queens_n = __builtin_popcountll(board->white_queens);
    int black_pawns_n = __builtin_popcountll(board->black_pawns);
    int black_knights_n = __builtin_popcountll(board->black_knights);
    int black_bishops_n = __builtin_popcountll(board->black_bishops);
    int black_rooks_n = __builtin_popcountll(board->black_rooks);
    int black_queens_n = __builtin_popcountll(board->black_queens);

    int white_can_mate = !cannot_force_mate(white_pawns_n, white_knights_n, white_bishops_n, white_rooks_n, white_queens_n);
    int black_can_mate = !cannot_force_mate(black_pawns_n, black_knights_n, black_bishops_n, black_rooks_n, black_queens_n);

    if (!white_can_mate && !black_can_mate) {
        return DRAW_SCORE;
    }

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
        score += KNIGHT_VALUE + knight_pst[sq];
        knights &= knights - 1;
    }

    knights = board->black_knights;
    while (knights) {
        int sq = __builtin_ctzll(knights);
        score -= KNIGHT_VALUE + knight_pst[sq ^ 56];
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
        score += BISHOP_VALUE + bishop_pst[sq];
        bishops &= bishops - 1;
    }

    bishops = board->black_bishops;
    while (bishops) {
        int sq = __builtin_ctzll(bishops);
        score -= BISHOP_VALUE + bishop_pst[sq ^ 56];
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
        score += ROOK_VALUE + rook_pst[sq];
        rooks &= rooks - 1;
    }

    rooks = board->black_rooks;
    while (rooks) {
        int sq = __builtin_ctzll(rooks);
        score -= ROOK_VALUE + rook_pst[sq ^ 56];
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
        score += QUEEN_VALUE + queen_pst[sq];
        queens &= queens - 1;
    }

    queens = board->black_queens;
    while (queens) {
        int sq = __builtin_ctzll(queens);
        score -= QUEEN_VALUE + queen_pst[sq ^ 56];
        queens &= queens - 1;
    }

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

    int phase = white_knights_n * KNIGHT_VALUE + white_bishops_n * BISHOP_VALUE
        + white_rooks_n * ROOK_VALUE + white_queens_n * QUEEN_VALUE
        + black_knights_n * KNIGHT_VALUE + black_bishops_n * BISHOP_VALUE
        + black_rooks_n * ROOK_VALUE + black_queens_n * QUEEN_VALUE;

    if (phase > MIDGAME_MATERIAL) {
        phase = MIDGAME_MATERIAL;
    }
    if (phase < ENDGAME_MATERIAL) {
        phase = ENDGAME_MATERIAL;
    }

    int midgame_weight = phase - ENDGAME_MATERIAL;
    int endgame_weight = MIDGAME_MATERIAL - phase;
    int weight_span = MIDGAME_MATERIAL - ENDGAME_MATERIAL;

    int white_king_p = board->white_kings ? __builtin_ctzll(board->white_kings) : -1;
    int black_king_p = board->black_kings ? __builtin_ctzll(board->black_kings) : -1;

    if (white_king_p >= 0) {
        score += (king_midgame_pst[white_king_p] * midgame_weight
            + king_endgame_pst[white_king_p] * endgame_weight) / weight_span;
    }
    if (black_king_p >= 0) {
        score -= (king_midgame_pst[black_king_p ^ 56] * midgame_weight
            + king_endgame_pst[black_king_p ^ 56] * endgame_weight) / weight_span;
    }

    if (white_king_p >= 0 && black_king_p >= 0) {
        int white_men = white_pawns_n + white_knights_n + white_bishops_n + white_rooks_n + white_queens_n;
        int black_men = black_pawns_n + black_knights_n + black_bishops_n + black_rooks_n + black_queens_n;
        int closeness = 7 - king_distance(white_king_p, black_king_p);

        if (black_men == 0 && white_can_mate) {
            score += MATE_DRIVE_EDGE * distance_from_centre(black_king_p) + MATE_DRIVE_CLOSE * closeness;
        } else if (white_men == 0 && black_can_mate) {
            score -= MATE_DRIVE_EDGE * distance_from_centre(white_king_p) + MATE_DRIVE_CLOSE * closeness;
        }
    }

    const pawn_hash_entry_t * pawn = cached_pawn_evaluation(board);

    score += piece_mobility(board, pawn, 1, midgame_weight, endgame_weight, weight_span);
    score -= piece_mobility(board, pawn, 0, midgame_weight, endgame_weight, weight_span);
    score += pawn->score;

    uint64_t white_passed = pawn->white_passed;
    uint64_t black_passed = pawn->black_passed;

    while (white_passed) {
        int sq = __builtin_ctzll(white_passed);
        score += passed_pawn_bonus(board, sq, 1, midgame_weight, endgame_weight, weight_span);
        white_passed &= white_passed - 1;
    }

    while (black_passed) {
        int sq = __builtin_ctzll(black_passed);
        score -= passed_pawn_bonus(board, sq, 0, midgame_weight, endgame_weight, weight_span);
        black_passed &= black_passed - 1;
    }

    return score;
}
