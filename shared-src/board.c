#include "board.h"
#include "zobrist.h"

char identify_piece(const board_t * board, int p) {
    uint64_t mask = (1ULL << p);

    if (board->white_pawns & mask)   return 'P';
    if (board->black_pawns & mask)   return 'p';
    if (board->white_knights & mask) return 'N';
    if (board->black_knights & mask) return 'n';
    if (board->white_bishops & mask) return 'B';
    if (board->black_bishops & mask) return 'b';
    if (board->white_rooks & mask)   return 'R';
    if (board->black_rooks & mask)   return 'r';
    if (board->white_queens & mask)  return 'Q';
    if (board->black_queens & mask)  return 'q';
    if (board->white_kings & mask)   return 'K';
    if (board->black_kings & mask)   return 'k';
    return ' ';
}

char identify_piece_of(const board_t * board, int p, int color) {
    uint64_t mask = (1ULL << p);
    int white = color == WHITE_COLOR;
    char lower = white ? 0 : 32; // 'a' - 'A'

    if (mask & (white ? board->white_pawns   : board->black_pawns))   return 'P' + lower;
    if (mask & (white ? board->white_knights : board->black_knights)) return 'N' + lower;
    if (mask & (white ? board->white_bishops : board->black_bishops)) return 'B' + lower;
    if (mask & (white ? board->white_rooks   : board->black_rooks))   return 'R' + lower;
    if (mask & (white ? board->white_queens  : board->black_queens))  return 'Q' + lower;
    if (mask & (white ? board->white_kings   : board->black_kings))   return 'K' + lower;
    return ' ';
}

void refresh_masks(board_t * board) {
    board->white_mask = board->white_pawns | board->white_knights | board->white_bishops
        | board->white_rooks | board->white_queens | board->white_kings;
    board->black_mask = board->black_pawns | board->black_knights | board->black_bishops
        | board->black_rooks | board->black_queens | board->black_kings;
}

void just_play_white_simple(board_t * board, const play_t * play) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    char to_x = play->to_x;
    char to_y = play->to_y;
    char promotion_option = play->promotion_option;

    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;

    char from_piece = identify_piece_of(board, from_p, WHITE_COLOR);

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    board->black_pawns &= ~to_mask;
    board->black_knights &= ~to_mask;
    board->black_bishops &= ~to_mask;
    board->black_rooks &= ~to_mask;
    board->black_queens &= ~to_mask;
    board->black_kings &= ~to_mask;

    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'P':
            board->white_pawns ^= from_mask;
            if (promotion_option == 0) {
                board->white_pawns ^= to_mask;
            } else {
                switch (promotion_option) {
                    case PROMOTION_QUEEN: board->white_queens ^= to_mask;   break;
                    case PROMOTION_KNIGHT: board->white_knights ^= to_mask; break;
                    case PROMOTION_BISHOP: board->white_bishops ^= to_mask; break;
                    case PROMOTION_ROOK: board->white_rooks ^= to_mask;     break;
                }
            }
            break;
        case 'N':
            board->white_knights ^= from_mask;
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= from_mask;
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= from_mask;
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= from_mask;
            board->white_queens ^= to_mask;
            break;
        case 'K':
            board->white_kings ^= from_mask;
            board->white_kings ^= to_mask;
            break;
    }

    if (board->en_passant_x == to_x) {
        if (from_y == 3 && from_piece == 'P') {
            board->black_pawns ^= (1ULL << (from_y * 8 + to_x));
        }
    }

    if (from_piece == 'K') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->white_rooks ^= (1ULL << (7 * 8 + 5));
                board->white_rooks ^= (1ULL << (7 * 8 + 7));
            } else if (to_x == 2) {
                board->white_rooks ^= (1ULL << (7 * 8 + 0));
                board->white_rooks ^= (1ULL << (7 * 8 + 3));
            }
        }
    }

    refresh_masks(board);
}

void just_play_black_simple(board_t * board, const play_t * play) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    char to_x = play->to_x;
    char to_y = play->to_y;
    char promotion_option = play->promotion_option;

    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;

    char from_piece = identify_piece_of(board, from_p, BLACK_COLOR);

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    board->white_pawns &= ~to_mask;
    board->white_knights &= ~to_mask;
    board->white_bishops &= ~to_mask;
    board->white_rooks &= ~to_mask;
    board->white_queens &= ~to_mask;
    board->white_kings &= ~to_mask;

    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'p':
            board->black_pawns ^= from_mask;
            if (promotion_option == 0) {
                board->black_pawns ^= to_mask;
            } else {
                switch (promotion_option) {
                    case PROMOTION_QUEEN: board->black_queens ^= to_mask;   break;
                    case PROMOTION_KNIGHT: board->black_knights ^= to_mask; break;
                    case PROMOTION_BISHOP: board->black_bishops ^= to_mask; break;
                    case PROMOTION_ROOK: board->black_rooks ^= to_mask;     break;
                }
            }
            break;
        case 'n':
            board->black_knights ^= from_mask;
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= from_mask;
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= from_mask;
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= from_mask;
            board->black_queens ^= to_mask;
            break;
        case 'k':
            board->black_kings ^= from_mask;
            board->black_kings ^= to_mask;
            break;
    }

    if (board->en_passant_x == to_x) {
        if (from_y == 4 && from_piece == 'p') {
            board->white_pawns ^= (1ULL << (from_y * 8 + to_x));
        }
    }

    if (from_piece == 'k') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->black_rooks ^= (1ULL << (0 * 8 + 5));
                board->black_rooks ^= (1ULL << (0 * 8 + 7));
            } else if (to_x == 2) {
                board->black_rooks ^= (1ULL << (0 * 8 + 0));
                board->black_rooks ^= (1ULL << (0 * 8 + 3));
            }
        }
    }

    refresh_masks(board);
}

void just_play_white_pawn(board_t * board, const play_t * play, int64_t * out_hash) {
    int from_x = play->from_x;
    int from_y = play->from_y;
    int to_x = play->to_x;
    int to_y = play->to_y;
    int en_passant_x = board->en_passant_x;
    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;
    char promotion_option = play->promotion_option;

    board->halfmoves = 0;

    char from_piece = 'P';
    char to_piece = identify_piece_of(board, to_p, BLACK_COLOR);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece(hash, to_p, to_piece);
    }

    hash ^= zobrist_side_to_move;

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'p':
            board->black_pawns ^= to_mask;
            break;
        case 'n':
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= to_mask;
            break;
        case 'k':
            board->black_kings ^= to_mask;
            break;
    }

    board->white_pawns ^= from_mask;

    if (en_passant_x != NO_EN_PASSANT) {
        if (en_passant_x == to_x && from_y == 3) {
            hash = update_hash_with_piece(hash, 3 * 8 + en_passant_x, 'p');
            board->black_pawns ^= 1ULL << (3 * 8 + en_passant_x);
        }
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    if (to_y == 0) {
        if (to_x == 0) {
            if (board->black_left_castling) {
                board->black_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
            }
        } else if (to_x == 7) {
            if (board->black_right_castling) {
                board->black_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
            }
        }

        if (promotion_option == PROMOTION_QUEEN) {
            board->white_queens ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'Q');
        } else if (promotion_option == PROMOTION_KNIGHT) {
            board->white_knights ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'N');
        } else if (promotion_option == PROMOTION_BISHOP) {
            board->white_bishops ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'B');
        } else {
            board->white_rooks ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'R');
        }
    } else {
        board->white_pawns ^= to_mask;
        hash = update_hash_with_piece(hash, to_p, 'P');

        if (from_y == 6 && to_y == 4) {
            board->en_passant_x = from_x;
            hash ^= zobrist_en_passant[from_x];
        }
    }

    board->color = BLACK_COLOR;

    *out_hash = hash;

    refresh_masks(board);
}

void just_play_white_complex(board_t * board, const play_t * play, int64_t * out_hash) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    int from_p = from_y * 8 + from_x;

    char from_piece = identify_piece_of(board, from_p, WHITE_COLOR);
    if (from_piece == 'P') {
        just_play_white_pawn(board, play, out_hash);
        return;
    }

    char to_x = play->to_x;
    char to_y = play->to_y;
    int to_p = to_y * 8 + to_x;

    char to_piece = identify_piece_of(board, to_p, BLACK_COLOR);

    board->halfmoves = (to_piece == ' ') ? board->halfmoves + 1 : 0;

    int64_t hash = *out_hash;

    hash = update_hash_with_piece(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece(hash, to_p, to_piece);
    }

    int en_passant_x = board->en_passant_x;
    if (en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'p':
            board->black_pawns ^= to_mask;
            break;
        case 'n':
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= to_mask;
            break;
        case 'k':
            board->black_kings ^= to_mask;
            break;
    }
    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'N':
            board->white_knights ^= from_mask;
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= from_mask;
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= from_mask;
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= from_mask;
            board->white_queens ^= to_mask;
            break;
        case 'K':
            board->white_kings ^= from_mask;
            board->white_kings ^= to_mask;
            break;
    }

    if (from_piece == 'K') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->white_rooks ^= (1ULL << (7 * 8 + 5));
                board->white_rooks ^= (1ULL << (7 * 8 + 7));
                hash = update_hash_with_piece(hash, 7 * 8 + 7, 'R');
                hash = update_hash_with_piece(hash, 7 * 8 + 5, 'R');
            } else if (to_x == 2) {
                board->white_rooks ^= (1ULL << (7 * 8 + 0));
                board->white_rooks ^= (1ULL << (7 * 8 + 3));
                hash = update_hash_with_piece(hash, 7 * 8 + 0, 'R');
                hash = update_hash_with_piece(hash, 7 * 8 + 3, 'R');
            }
        }

        if (board->white_right_castling) {
            board->white_right_castling = 0;
            hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
        }
        if (board->white_left_castling) {
            board->white_left_castling = 0;
            hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
        }
    } else {
        if (from_y == 7) {
            if (from_x == 0) {
                if (board->white_left_castling) {
                    board->white_left_castling = 0;
                    hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
                }
            } else if (from_x == 7) {
                if (board->white_right_castling) {
                    board->white_right_castling = 0;
                    hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
                }
            }
        }
    }
    if (to_y == 0) {
        if (to_x == 0) {
            if (board->black_left_castling) {
                board->black_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
            }
        } else if (to_x == 7) {
            if (board->black_right_castling) {
                board->black_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
            }
        }
    }

    hash = update_hash_with_piece(hash, to_p, from_piece);

    hash ^= zobrist_side_to_move;

    board->color = BLACK_COLOR;

    *out_hash = hash;

    refresh_masks(board);
}

void just_play_black_pawn(board_t * board, const play_t * play, int64_t * out_hash) {
    int from_x = play->from_x;
    int from_y = play->from_y;
    int to_x = play->to_x;
    int to_y = play->to_y;
    int en_passant_x = board->en_passant_x;
    int from_p = from_y * 8 + from_x;
    int to_p = to_y * 8 + to_x;
    char promotion_option = play->promotion_option;

    board->halfmoves = 0;

    char from_piece = 'p';
    char to_piece = identify_piece_of(board, to_p, WHITE_COLOR);

    int64_t hash = *out_hash;

    hash = update_hash_with_piece(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece(hash, to_p, to_piece);
    }

    hash ^= zobrist_side_to_move;

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'P':
            board->white_pawns ^= to_mask;
            break;
        case 'N':
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= to_mask;
            break;
        case 'K':
            board->white_kings ^= to_mask;
            break;
    }

    board->black_pawns ^= from_mask;

    if (en_passant_x != NO_EN_PASSANT) {
        if (en_passant_x == to_x && from_y == 4) {
            hash = update_hash_with_piece(hash, 4 * 8 + en_passant_x, 'P');
            board->white_pawns ^= 1ULL << (4 * 8 + en_passant_x);
        }
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    if (to_y == 7) {
        if (to_x == 0) {
            if (board->white_left_castling) {
                board->white_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
            }
        } else if (to_x == 7) {
            if (board->white_right_castling) {
                board->white_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
            }
        }

        if (promotion_option == PROMOTION_QUEEN) {
            board->black_queens ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'q');
        } else if (promotion_option == PROMOTION_KNIGHT) {
            board->black_knights ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'n');
        } else if (promotion_option == PROMOTION_BISHOP) {
            board->black_bishops ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'b');
        } else {
            board->black_rooks ^= to_mask;
            hash = update_hash_with_piece(hash, to_p, 'r');
        }
    } else {
        board->black_pawns ^= to_mask;
        hash = update_hash_with_piece(hash, to_p, 'p');

        if (from_y == 1 && to_y == 3) {
            board->en_passant_x = from_x;
            hash ^= zobrist_en_passant[from_x];
        }
    }

    board->color = WHITE_COLOR;

    *out_hash = hash;

    refresh_masks(board);
}

void just_play_black_complex(board_t * board, const play_t * play, int64_t * out_hash) {
    char from_x = play->from_x;
    char from_y = play->from_y;
    int from_p = from_y * 8 + from_x;

    char from_piece = identify_piece_of(board, from_p, BLACK_COLOR);
    if (from_piece == 'p') {
        just_play_black_pawn(board, play, out_hash);
        return;
    }

    char to_x = play->to_x;
    char to_y = play->to_y;
    int to_p = to_y * 8 + to_x;

    char to_piece = identify_piece_of(board, to_p, WHITE_COLOR);

    board->halfmoves = (to_piece == ' ') ? board->halfmoves + 1 : 0;

    int64_t hash = *out_hash;

    hash = update_hash_with_piece(hash, from_p, from_piece);
    if (to_piece != ' ') {
        hash = update_hash_with_piece(hash, to_p, to_piece);
    }

    int en_passant_x = board->en_passant_x;
    if (en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[en_passant_x];
        board->en_passant_x = NO_EN_PASSANT;
    }

    uint64_t from_mask = 1ULL << from_p;
    uint64_t to_mask = 1ULL << to_p;

    // Remove captured piece, at destination
    switch (to_piece) {
        case 'P':
            board->white_pawns ^= to_mask;
            break;
        case 'N':
            board->white_knights ^= to_mask;
            break;
        case 'B':
            board->white_bishops ^= to_mask;
            break;
        case 'R':
            board->white_rooks ^= to_mask;
            break;
        case 'Q':
            board->white_queens ^= to_mask;
            break;
        case 'K':
            board->white_kings ^= to_mask;
            break;
    }
    // Remove moved piece from origin and add it to destination
    switch (from_piece) {
        case 'n':
            board->black_knights ^= from_mask;
            board->black_knights ^= to_mask;
            break;
        case 'b':
            board->black_bishops ^= from_mask;
            board->black_bishops ^= to_mask;
            break;
        case 'r':
            board->black_rooks ^= from_mask;
            board->black_rooks ^= to_mask;
            break;
        case 'q':
            board->black_queens ^= from_mask;
            board->black_queens ^= to_mask;
            break;
        case 'k':
            board->black_kings ^= from_mask;
            board->black_kings ^= to_mask;
            break;
    }

    if (from_piece == 'k') {
        if (from_x == 4) {
            if (to_x == 6) {
                board->black_rooks ^= (1ULL << (0 * 8 + 5));
                board->black_rooks ^= (1ULL << (0 * 8 + 7));
                hash = update_hash_with_piece(hash, 0 * 8 + 7, 'r');
                hash = update_hash_with_piece(hash, 0 * 8 + 5, 'r');
            } else if (to_x == 2) {
                board->black_rooks ^= (1ULL << (0 * 8 + 0));
                board->black_rooks ^= (1ULL << (0 * 8 + 3));
                hash = update_hash_with_piece(hash, 0 * 8 + 0, 'r');
                hash = update_hash_with_piece(hash, 0 * 8 + 3, 'r');
            }
        }

        if (board->black_right_castling) {
            board->black_right_castling = 0;
            hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
        }
        if (board->black_left_castling) {
            board->black_left_castling = 0;
            hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
        }
    } else {
        if (from_y == 0) {
            if (from_x == 0) {
                if (board->black_left_castling) {
                    board->black_left_castling = 0;
                    hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
                }
            } else if (from_x == 7) {
                if (board->black_right_castling) {
                    board->black_right_castling = 0;
                    hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
                }
            }
        }
    }
    if (to_y == 7) {
        if (to_x == 0) {
            if (board->white_left_castling) {
                board->white_left_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
            }
        } else if (to_x == 7) {
            if (board->white_right_castling) {
                board->white_right_castling = 0;
                hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
            }
        }
    }

    hash = update_hash_with_piece(hash, to_p, from_piece);

    hash ^= zobrist_side_to_move;

    board->color = WHITE_COLOR;

    *out_hash = hash;

    refresh_masks(board);
}

void actual_play(board_t * board, board_ext_t * board_ext, const play_t * play) {
    if (board_ext->past_plays_count < MAX_GAME_PLAYS) {
        board_ext->past_plays[board_ext->past_plays_count].from_x = play->from_x;
        board_ext->past_plays[board_ext->past_plays_count].from_y = play->from_y;
        board_ext->past_plays[board_ext->past_plays_count].to_x = play->to_x;
        board_ext->past_plays[board_ext->past_plays_count].to_y = play->to_y;
        board_ext->past_plays[board_ext->past_plays_count].promotion_option = play->promotion_option;
        board_ext->past_hashes[board_ext->past_plays_count] = hash_from_board(board);
        board_ext->past_plays_count++;
    }

    board_ext->last_play_x = play->to_x;
    board_ext->last_play_y = play->to_y;
    if (board->color == BLACK_COLOR) {
        board_ext->fullmoves++;
    }

    int64_t hash = 0;
    if (board->color == WHITE_COLOR) {
        just_play_white_complex(board, play, &hash);
    } else {
        just_play_black_complex(board, play, &hash);
    }
}

int64_t hash_from_board(const board_t * board) {
    int64_t hash = 0xAAAAAAAAAAAAAAAA;

    for (int p = 0; p < 64; ++p) {
        char piece = identify_piece(board, p);
        if (piece != ' ') {
            hash = update_hash_with_piece(hash, p, piece);
        }
    }

    if (board->color == BLACK_COLOR) {
        hash ^= zobrist_side_to_move;
    }

    if (board->en_passant_x != NO_EN_PASSANT) {
        hash ^= zobrist_en_passant[(int)(board->en_passant_x)];
    }

    if (board->white_right_castling) {
        hash ^= zobrist_castling[CASTLING_WHITE_RIGHT];
    }
    if (board->white_left_castling) {
        hash ^= zobrist_castling[CASTLING_WHITE_LEFT];
    }
    if (board->black_right_castling) {
        hash ^= zobrist_castling[CASTLING_BLACK_RIGHT];
    }
    if (board->black_left_castling) {
        hash ^= zobrist_castling[CASTLING_BLACK_LEFT];
    }

    return hash;
}

void reset_board() {
    const char * pieces = "rnbqkbnrpppppppp                                PPPPPPPPRNBQKBNR";

    board.white_pawns = 0;
    board.black_pawns = 0;
    board.white_knights = 0;
    board.black_knights = 0;
    board.white_bishops = 0;
    board.black_bishops = 0;
    board.white_rooks = 0;
    board.black_rooks = 0;
    board.white_queens = 0;
    board.black_queens = 0;
    board.white_kings = 0;
    board.black_kings = 0;

    for (int p = 0; p < 64; ++p) {
        uint64_t bit = 1ULL << p;

        switch (pieces[p]) {
            case 'P': board.white_pawns   |= bit; break;
            case 'p': board.black_pawns   |= bit; break;
            case 'N': board.white_knights |= bit; break;
            case 'n': board.black_knights |= bit; break;
            case 'B': board.white_bishops |= bit; break;
            case 'b': board.black_bishops |= bit; break;
            case 'R': board.white_rooks   |= bit; break;
            case 'r': board.black_rooks   |= bit; break;
            case 'Q': board.white_queens  |= bit; break;
            case 'q': board.black_queens  |= bit; break;
            case 'K': board.white_kings   |= bit; break;
            case 'k': board.black_kings   |= bit; break;
        }
    }

    board.en_passant_x = NO_EN_PASSANT;
    board.white_left_castling = 1;
    board.white_right_castling = 1;
    board.black_left_castling = 1;
    board.black_right_castling = 1;
    board.color = WHITE_COLOR;
    board.halfmoves = 0;
    refresh_masks(&board);
    board_ext.fullmoves = 1;
    board_ext.past_plays_count = 0;
    board_ext.last_play_x = -1;
}

void undo_last_plays() {
    unsigned past_plays = board_ext.past_plays_count - 2;
    reset_board();
    for (unsigned int i = 0; i < past_plays; ++i) {
        actual_play(&board, &board_ext, &board_ext.past_plays[i]);
    }
    board_ext.past_plays_count = past_plays;
}

// True only for dead positions, i.e. where no sequence of legal moves by either side can produce a
// checkmate. That is K vs K, K plus a single minor vs K, and K+B vs K+B with both bishops on squares
// of the same color. Everything else, K+N+N vs K included, is still playable: a mate cannot be
// forced there but it can be reached, so the search is left to work it out rather than being told
// the game is over.
int insufficient_material(const board_t * board) {
    if (board->white_queens || board->black_queens || board->white_rooks || board->black_rooks || board->white_pawns || board->black_pawns) {
        return 0;
    }

    int white_knights = __builtin_popcountll(board->white_knights);
    int black_knights = __builtin_popcountll(board->black_knights);
    int white_bishops = __builtin_popcountll(board->white_bishops);
    int black_bishops = __builtin_popcountll(board->black_bishops);

    int white_bns = white_knights + white_bishops;
    int black_bns = black_knights + black_bishops;

    // K vs K, and K plus one minor vs K
    if (white_bns + black_bns <= 1) {
        return 1;
    }

    // K+B vs K+B, drawn when both bishops travel on the same color squares
    if (white_bishops == 1 && black_bishops == 1 && white_bns == 1 && black_bns == 1) {
        int white_sq = __builtin_ctzll(board->white_bishops);
        int black_sq = __builtin_ctzll(board->black_bishops);
        int white_color = ((white_sq >> 3) + (white_sq & 7)) & 1;
        int black_color = ((black_sq >> 3) + (black_sq & 7)) & 1;
        return white_color == black_color;
    }

    return 0;
}

int is_game_drawn() {
    if (insufficient_material(&board) || board.halfmoves >= 100) {
        return 1;
    }

    // Threefold repetition, the position on the board being the third occurrence.
    int64_t hash = hash_from_board(&board);
    int seen = 0;

    for (unsigned int i = 0; i < board_ext.past_plays_count; ++i) {
        if (board_ext.past_hashes[i] == hash) {
            seen++;
        }
    }

    return seen >= 2;
}
