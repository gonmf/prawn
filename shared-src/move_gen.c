#include "common.h"

int piece_value(char piece) {
    switch (piece) {
        case 'P':
        case 'p':
            return PAWN_VALUE;
        case 'N':
        case 'n':
            return KNIGHT_VALUE;
        case 'R':
        case 'r':
            return ROOK_VALUE;
        case 'B':
        case 'b':
            return BISHOP_VALUE;
        case 'Q':
        case 'q':
            return QUEEN_VALUE;
        case 'K':
        case 'k':
            return KING_VALUE;
        default:
            return 0;
    }
}

// Ignores capturing via en passant
int square_attacked_by(const board_t * board, int sq, int by_color) {
    uint64_t white_mask = board->white_mask;
    uint64_t black_mask = board->black_mask;
    uint64_t occupied = white_mask | black_mask;

    uint64_t knights, kings, pawns, pawn_origins, rooks_queens, bishops_queens;
    if (by_color == WHITE_COLOR) {
        knights = board->white_knights;
        kings = board->white_kings;
        pawns = board->white_pawns;
        pawn_origins = black_pawn_capture_masks[sq];
        rooks_queens = board->white_rooks | board->white_queens;
        bishops_queens = board->white_bishops | board->white_queens;
    } else {
        knights = board->black_knights;
        kings = board->black_kings;
        pawns = board->black_pawns;
        pawn_origins = white_pawn_capture_masks[sq];
        rooks_queens = board->black_rooks | board->black_queens;
        bishops_queens = board->black_bishops | board->black_queens;
    }

    if (knight_moves_masks[sq] & knights) {
        return 1;
    }
    if (king_moves_masks[sq] & kings) {
        return 1;
    }
    if (pawn_origins & pawns) {
        return 1;
    }

    if (rooks_queens & magic_rook_attacks(sq, occupied)) {
        return 1;
    }
    if (bishops_queens & magic_bishop_attacks(sq, occupied)) {
        return 1;
    }

    return 0;
}

uint64_t compute_pins(const board_t * board, int king_p, int own_color, uint64_t * pin_ray) {
    uint64_t white_mask = board->white_mask;
    uint64_t black_mask = board->black_mask;
    uint64_t occupied = white_mask | black_mask;

    uint64_t own, opponent_rooks_queens, opponent_bishops_queens;
    if (own_color == WHITE_COLOR) {
        own = white_mask;
        opponent_rooks_queens = board->black_rooks | board->black_queens;
        opponent_bishops_queens = board->black_bishops | board->black_queens;
    } else {
        own = black_mask;
        opponent_rooks_queens = board->white_rooks | board->white_queens;
        opponent_bishops_queens = board->white_bishops | board->white_queens;
    }

    static const int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
    };

    uint64_t pinned = 0;
    int from_x = king_p % 8;
    int from_y = king_p / 8;

    for (int d = 0; d < 8; ++d) {
        uint64_t sliders = d < 4 ? opponent_rooks_queens : opponent_bishops_queens;
        if (sliders == 0) {
            continue;
        }

        int dx = directions[d][0];
        int dy = directions[d][1];
        int x = from_x + dx;
        int y = from_y + dy;
        uint64_t ray = 0;
        int candidate = -1;

        while (x >= 0 && x < 8 && y >= 0 && y < 8) {
            int to = y * 8 + x;
            uint64_t to_mask = 1ULL << to;
            ray |= to_mask;

            if (occupied & to_mask) {
                if (candidate == -1) {
                    if (!(own & to_mask)) {
                        break;
                    }
                    candidate = to;
                } else {
                    if (sliders & to_mask) {
                        pinned |= 1ULL << candidate;
                        pin_ray[candidate] = ray;
                    }
                    break;
                }
            }

            x += dx;
            y += dy;
        }
    }

    return pinned;
}

uint64_t attackers_to_square(const board_t * board, int sq, uint64_t occupied) {
    uint64_t attackers = 0;

    attackers |= knight_moves_masks[sq] & (board->white_knights | board->black_knights);
    attackers |= king_moves_masks[sq] & (board->white_kings | board->black_kings);
    attackers |= white_pawn_capture_masks[sq] & board->black_pawns;
    attackers |= black_pawn_capture_masks[sq] & board->white_pawns;

    uint64_t rooks_queens = board->white_rooks | board->black_rooks | board->white_queens | board->black_queens;
    uint64_t bishops_queens = board->white_bishops | board->black_bishops | board->white_queens | board->black_queens;

    attackers |= rooks_queens & magic_rook_attacks(sq, occupied);
    attackers |= bishops_queens & magic_bishop_attacks(sq, occupied);

    return attackers & occupied;
}

int static_exchange_eval(const board_t * board, const play_t * play, int mover_is_white) {
    int from_p = play->from_y * 8 + play->from_x;
    int to_p = play->to_y * 8 + play->to_x;

    char victim = identify_piece_of(board, to_p, mover_is_white ? BLACK_COLOR : WHITE_COLOR);
    if (victim == ' ') {
        return 0;
    }
    if (play->promotion_option != 0) {
        return QUEEN_VALUE;
    }

    char attacker = identify_piece_of(board, from_p, mover_is_white ? WHITE_COLOR : BLACK_COLOR);

    uint64_t white_mask = board->white_mask;
    uint64_t black_mask = board->black_mask;
    uint64_t occupied = (white_mask | black_mask) ^ (1ULL << from_p);

    int gain[32];
    int d = 0;
    gain[0] = piece_value(victim);
    int attacker_value = piece_value(attacker);
    int white_to_move = !mover_is_white;

    while (d < 30) {
        d++;
        gain[d] = attacker_value - gain[d - 1];

        uint64_t attackers = attackers_to_square(board, to_p, occupied);
        uint64_t own = attackers & (white_to_move ? white_mask : black_mask);
        if (own == 0) {
            break;
        }

        int cheapest_sq = -1;
        int cheapest_value = KING_VALUE + 1;
        uint64_t bits = own;
        while (bits) {
            int s = __builtin_ctzll(bits);
            int v = piece_value(identify_piece_of(board, s, white_to_move ? WHITE_COLOR : BLACK_COLOR));
            if (v < cheapest_value) {
                cheapest_value = v;
                cheapest_sq = s;
            }
            bits &= bits - 1;
        }

        if (cheapest_value == KING_VALUE && (attackers & (white_to_move ? black_mask : white_mask)) != 0) {
            break;
        }

        occupied ^= 1ULL << cheapest_sq;
        attacker_value = cheapest_value;
        white_to_move = !white_to_move;
    }

    while (--d > 0) {
        int gain1 = -gain[d - 1];
        int gain2 = gain[d];
        gain[d - 1] = -MAX(gain1, gain2);
    }

    return gain[0];
}

void populate_pawn_capture_masks() {
    int x2, y2;

    for (int p = 0; p < 64; ++p) {
        white_pawn_capture_masks[p] = 0ULL;
        black_pawn_capture_masks[p] = 0ULL;

        int x = p % 8;
        int y = p / 8;

        x2 = x - 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x - 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_pawn_capture_masks[p] |= (1ULL << (y2 * 8 + x2));
        }
    }

    for (int x = 0; x < 8; ++x) {
        white_en_passant_capture_masks[x] = 0ULL;
        black_en_passant_capture_masks[x] = 0ULL;

        int y = 2;

        x2 = x - 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y + 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            white_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }

        y = 5;

        x2 = x - 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }

        x2 = x + 1;
        y2 = y - 1;
        if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
            black_en_passant_capture_masks[x] |= (1ULL << (y2 * 8 + x2));
        }
    }
}

void populate_knight_moves_masks() {
    const int offsets[8][2] = {
        {-1, -2}, {+1, -2}, {-2, -1}, {+2, -1},
        {-2, +1}, {+2, +1}, {-1, +2}, {+1, +2}
    };

    for (int p = 0; p < 64; ++p) {
        int x = p % 8, y = p / 8;
        knight_moves_masks[p] = 0ULL;

        for (int i = 0; i < 8; i++) {
            int x2 = x + offsets[i][0];
            int y2 = y + offsets[i][1];
            if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
                knight_moves_masks[p] |= (1ULL << (y2 * 8 + x2));
            }
        }
    }
}

void populate_king_moves_masks() {
    const int offsets[8][2] = {
        {-1, -1}, {0, -1}, {+1, -1},
        {-1,  0},          {+1,  0},
        {-1, +1}, {0, +1}, {+1, +1}
    };

    for (int p = 0; p < 64; ++p) {
        int x = p % 8, y = p / 8;
        king_moves_masks[p] = 0ULL;

        for (int i = 0; i < 8; i++) {
            int x2 = x + offsets[i][0];
            int y2 = y + offsets[i][1];
            if (x2 >= 0 && x2 < 8 && y2 >= 0 && y2 < 8) {
                king_moves_masks[p] |= (1ULL << (y2 * 8 + x2));
            }
        }
    }
}

// Enumerates all apparently possible moves, disregarding pins to the kind and
// castlting when attacked or through attacked squares.
int enumerate_all_piece_moves(play_t * valid_plays, const board_t * board, int captures_only) {
    int valid_plays_i = 0;

    int white_to_play = board->color == WHITE_COLOR;
    uint64_t own_mask = white_to_play ? board->white_mask : board->black_mask;
    uint64_t opponent_mask = white_to_play ? board->black_mask : board->white_mask;
    uint64_t empty_mask = ~(own_mask | opponent_mask);
    uint64_t destination_mask = captures_only ? opponent_mask : (empty_mask | opponent_mask);

    uint64_t own_pawns = white_to_play ? board->white_pawns : board->black_pawns;
    uint64_t own_knights = white_to_play ? board->white_knights : board->black_knights;
    uint64_t own_bishops = white_to_play ? board->white_bishops : board->black_bishops;
    uint64_t own_rooks = white_to_play ? board->white_rooks : board->black_rooks;
    uint64_t own_queens = white_to_play ? board->white_queens : board->black_queens;
    uint64_t own_kings = white_to_play ? board->white_kings : board->black_kings;

    int push_back = white_to_play ? 8 : -8;
    int promotion_rank = white_to_play ? 0 : 7;
    int home_rank = white_to_play ? 7 : 0;
    int en_passant_to_y = white_to_play ? 2 : 5;
    uint64_t promotion_rank_mask = white_to_play ? 0x00000000000000FFULL : 0xFF00000000000000ULL;
    uint64_t double_push_mask = white_to_play ? 0x000000FF00000000ULL : 0x00000000FF000000ULL;
    const uint64_t * pawn_capture_masks = white_to_play ? white_pawn_capture_masks : black_pawn_capture_masks;
    const uint64_t * ep_capture_masks = white_to_play ? white_en_passant_capture_masks : black_en_passant_capture_masks;
    int left_castling = white_to_play ? board->white_left_castling : board->black_left_castling;
    int right_castling = white_to_play ? board->white_right_castling : board->black_right_castling;

    uint64_t moves = (white_to_play ? (own_pawns >> 8) : (own_pawns << 8)) & empty_mask;

    // A push to the last rank is worth as much as a capture, don't filter
    if (captures_only) {
        moves &= promotion_rank_mask;
    }

    // Pawn single move forward and promotion
    while (moves) {
        int to = __builtin_ctzll(moves);
        int from = to + push_back;

        int to_x = to % 8;
        int to_y = to / 8;
        int from_x = from % 8;
        int from_y = from / 8;

        if (to_y == promotion_rank) {
            valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
            valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
        } else {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;
        }

        moves &= moves - 1;
    }

    // Pawn double move forward
    moves = 0ULL;
    if (!captures_only) {
        uint64_t single = (white_to_play ? (own_pawns >> 8) : (own_pawns << 8)) & empty_mask;
        moves = (white_to_play ? (single >> 8) : (single << 8)) & empty_mask & double_push_mask;
    }

    while (moves) {
        int to = __builtin_ctzll(moves);
        int from = to + 2 * push_back;

        int to_x = to % 8;
        int to_y = to / 8;
        int from_x = from % 8;
        int from_y = from / 8;

        valid_plays[valid_plays_i].promotion_option = 0;
        valid_plays[valid_plays_i].from_x = from_x;
        valid_plays[valid_plays_i].from_y = from_y;
        valid_plays[valid_plays_i].to_x = to_x;
        valid_plays[valid_plays_i].to_y = to_y;
        valid_plays_i = valid_plays_i + 1;

        moves &= moves - 1;
    }

    // Pawn captures
    moves = own_pawns;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = pawn_capture_masks[from] & opponent_mask;
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            if (to_y == promotion_rank) {
                valid_plays[valid_plays_i].promotion_option = PROMOTION_QUEEN;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_KNIGHT;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_BISHOP;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
                valid_plays[valid_plays_i].promotion_option = PROMOTION_ROOK;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
            } else {
                valid_plays[valid_plays_i].promotion_option = 0;
                valid_plays[valid_plays_i].from_x = from_x;
                valid_plays[valid_plays_i].from_y = from_y;
                valid_plays[valid_plays_i].to_x = to_x;
                valid_plays[valid_plays_i].to_y = to_y;
                valid_plays_i = valid_plays_i + 1;
            }

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // Pawn captures via en passant
    if (board->en_passant_x != NO_EN_PASSANT) {
        moves = ep_capture_masks[(int)board->en_passant_x] & own_pawns;
        while (moves) {
            int from = __builtin_ctzll(moves);
            int from_x = from % 8;
            int from_y = from / 8;
            int to_x = board->en_passant_x;
            int to_y = en_passant_to_y;

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves &= moves - 1;
        }
    }

    // Knight moves
    moves = own_knights;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = knight_moves_masks[from] & destination_mask;
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // King moves
    moves = own_kings;
    while (moves) {
        int from = __builtin_ctzll(moves);
        int from_x = from % 8;
        int from_y = from / 8;

        uint64_t moves_to = king_moves_masks[from] & destination_mask;
        while (moves_to) {
            int to = __builtin_ctzll(moves_to);
            int to_x = to % 8;
            int to_y = to / 8;

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from_x;
            valid_plays[valid_plays_i].from_y = from_y;
            valid_plays[valid_plays_i].to_x = to_x;
            valid_plays[valid_plays_i].to_y = to_y;
            valid_plays_i = valid_plays_i + 1;

            moves_to &= moves_to - 1;
        }

        moves &= moves - 1;
    }

    // King castling
    if (!captures_only && (own_kings & (1ULL << (home_rank * 8 + 4)))) {
        if (left_castling && (empty_mask & (1ULL << (home_rank * 8 + 1))) && (empty_mask & (1ULL << (home_rank * 8 + 2))) && (empty_mask & (1ULL << (home_rank * 8 + 3)))) {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = 4;
            valid_plays[valid_plays_i].from_y = home_rank;
            valid_plays[valid_plays_i].to_x = 2;
            valid_plays[valid_plays_i].to_y = home_rank;
            valid_plays_i = valid_plays_i + 1;
        }
        if (right_castling && (empty_mask & (1ULL << (home_rank * 8 + 5))) && (empty_mask & (1ULL << (home_rank * 8 + 6)))) {
            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = 4;
            valid_plays[valid_plays_i].from_y = home_rank;
            valid_plays[valid_plays_i].to_x = 6;
            valid_plays[valid_plays_i].to_y = home_rank;
            valid_plays_i = valid_plays_i + 1;
        }
    }

    // Rooks, bishops and queens: the reachable squares come from one table lookup each, so the
    // plays are laid down in square order rather than ray by ray.
    moves = own_rooks | own_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        uint64_t targets = magic_rook_attacks(from, own_mask | opponent_mask) & ~own_mask;

        if (captures_only) {
            targets &= opponent_mask;
        }

        while (targets) {
            int to = __builtin_ctzll(targets);

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from % 8;
            valid_plays[valid_plays_i].from_y = from / 8;
            valid_plays[valid_plays_i].to_x = to % 8;
            valid_plays[valid_plays_i].to_y = to / 8;
            valid_plays_i++;

            targets &= targets - 1;
        }

        moves &= moves - 1;
    }

    moves = own_bishops | own_queens;
    while (moves) {
        int from = __builtin_ctzll(moves);
        uint64_t targets = magic_bishop_attacks(from, own_mask | opponent_mask) & ~own_mask;

        if (captures_only) {
            targets &= opponent_mask;
        }

        while (targets) {
            int to = __builtin_ctzll(targets);

            valid_plays[valid_plays_i].promotion_option = 0;
            valid_plays[valid_plays_i].from_x = from % 8;
            valid_plays[valid_plays_i].from_y = from / 8;
            valid_plays[valid_plays_i].to_x = to % 8;
            valid_plays[valid_plays_i].to_y = to / 8;
            valid_plays_i++;

            targets &= targets - 1;
        }

        moves &= moves - 1;
    }

    return valid_plays_i;
}

// Enumerates all legal plays with the help of enumerate_all_piece_moves.
int enumerate_legal_plays(play_t * valid_plays, const board_t * board, int captures_only, int * out_in_check) {
    int valid_plays_i = 0;
    play_t valid_plays_local[218];
    board_t board_cpy;

    int white_to_play = board->color == WHITE_COLOR;
    int own_color = white_to_play ? WHITE_COLOR : BLACK_COLOR;
    int opponent_color = white_to_play ? BLACK_COLOR : WHITE_COLOR;
    uint64_t own_kings = white_to_play ? board->white_kings : board->black_kings;
    uint64_t own_pawns = white_to_play ? board->white_pawns : board->black_pawns;
    int home_rank = white_to_play ? 7 : 0;
    int en_passant_from_y = white_to_play ? 3 : 4;

    int king_on_start = (own_kings & (1ULL << (home_rank * 8 + 4))) != 0ULL;
    int king_p = own_kings ? __builtin_ctzll(own_kings) : -1;

    int in_check = 1;
    uint64_t pinned = 0;
    uint64_t pin_ray[64];

    if (king_p >= 0) {
        in_check = square_attacked_by(board, king_p, opponent_color);
        if (!in_check) {
            pinned = compute_pins(board, king_p, own_color, pin_ray);
        }
    }

    if (out_in_check != NULL) {
        *out_in_check = in_check;
    }

    // No evasion is optional, so being in check overrides a request for captures alone
    if (in_check) {
        captures_only = 0;
    }

    int valid_plays_local_i = enumerate_all_piece_moves(valid_plays_local, board, captures_only);

    // Detect if playing exposes king to immediate capture (illegal move)
    for (int i = 0; i < valid_plays_local_i; ++i) {
        int from_p = valid_plays_local[i].from_y * 8 + valid_plays_local[i].from_x;
        int to_p = valid_plays_local[i].to_y * 8 + valid_plays_local[i].to_x;

        int en_passant = board->en_passant_x == valid_plays_local[i].to_x
            && valid_plays_local[i].from_y == en_passant_from_y
            && (own_pawns & (1ULL << from_p)) != 0ULL;

        if (!in_check && from_p != king_p && !en_passant) {
            if ((pinned & (1ULL << from_p)) && !(pin_ray[from_p] & (1ULL << to_p))) {
                continue;
            }
        } else {
            memcpy(&board_cpy, board, sizeof(board_t));
            if (white_to_play) {
                just_play_white_simple(&board_cpy, &valid_plays_local[i]);
            } else {
                just_play_black_simple(&board_cpy, &valid_plays_local[i]);
            }

            uint64_t moved_kings = white_to_play ? board_cpy.white_kings : board_cpy.black_kings;
            if (moved_kings && square_attacked_by(&board_cpy, __builtin_ctzll(moved_kings), opponent_color)) {
                continue;
            }

            int from_x = valid_plays_local[i].from_x;
            int from_y = valid_plays_local[i].from_y;

            // The square the king passes over has to be safe, too
            if (king_on_start && from_x == 4 && from_y == home_rank) {
                int to_x = valid_plays_local[i].to_x;

                if (to_x == 6) {
                    if (in_check || square_attacked_by(&board_cpy, home_rank * 8 + 5, opponent_color)) {
                        continue;
                    }
                } else if (to_x == 2) {
                    if (in_check || square_attacked_by(&board_cpy, home_rank * 8 + 3, opponent_color)) {
                        continue;
                    }
                }
            }
        }

        valid_plays[valid_plays_i] = valid_plays_local[i];
        valid_plays_i++;
    }

    return valid_plays_i;
}

int king_threatened(const board_t * board) {
    if (board->color == WHITE_COLOR) {
        return board->white_kings != 0ULL && square_attacked_by(board, __builtin_ctzll(board->white_kings), BLACK_COLOR);
    } else {
        return board->black_kings != 0ULL && square_attacked_by(board, __builtin_ctzll(board->black_kings), WHITE_COLOR);
    }
}
