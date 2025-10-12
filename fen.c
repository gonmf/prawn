#include "common.h"

void fen_to_board(board_t * board, board_ext_t * board_ext, const char * fen_str) {
    board->white_pawns = 0;
    board->black_pawns = 0;
    board->white_knights = 0;
    board->black_knights = 0;
    board->white_bishops = 0;
    board->black_bishops = 0;
    board->white_rooks = 0;
    board->black_rooks = 0;
    board->white_queens = 0;
    board->black_queens = 0;
    board->white_kings = 0;
    board->black_kings = 0;

    int fen_str_i = 0;

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (fen_str[fen_str_i] >= '1' && fen_str[fen_str_i] <= '8') {
                x += fen_str[fen_str_i] - '1';
            } else {
                uint64_t bit = 1ULL << (y * 8 + x);

                switch (fen_str[fen_str_i]) {
                    case 'P': board->white_pawns   |= bit; break;
                    case 'p': board->black_pawns   |= bit; break;
                    case 'N': board->white_knights |= bit; break;
                    case 'n': board->black_knights |= bit; break;
                    case 'B': board->white_bishops |= bit; break;
                    case 'b': board->black_bishops |= bit; break;
                    case 'R': board->white_rooks   |= bit; break;
                    case 'r': board->black_rooks   |= bit; break;
                    case 'Q': board->white_queens  |= bit; break;
                    case 'q': board->black_queens  |= bit; break;
                    case 'K': board->white_kings   |= bit; break;
                    case 'k': board->black_kings   |= bit; break;
                }
            }
            fen_str_i++;
        }

       fen_str_i++;
    }

    if (fen_str[fen_str_i] == 'w') {
        board->color = WHITE_COLOR;
    } else {
        board->color = BLACK_COLOR;
    }

    fen_str_i += 2;

    board->white_left_castling = 0;
    board->white_right_castling = 0;
    board->black_left_castling = 0;
    board->black_right_castling = 0;

    if (fen_str[fen_str_i] == 'K') {
        board->white_right_castling = 1;
        fen_str_i++;
    }
    if (fen_str[fen_str_i] == 'Q') {
        board->white_left_castling = 1;
        fen_str_i++;
    }
    if (fen_str[fen_str_i] == 'k') {
        board->black_right_castling = 1;
        fen_str_i++;
    }
    if (fen_str[fen_str_i] == 'q') {
        board->black_left_castling = 1;
    }

    fen_str_i += 2;

    if (fen_str[fen_str_i] == '-') {
        board->en_passant_x = NO_EN_PASSANT;
        fen_str_i += 2;
    } else {
        board->en_passant_x = fen_str[fen_str_i] - 'a';
        fen_str_i += 3;
    }

    board->halfmoves = 0;
    while (fen_str[fen_str_i] >= '0' && fen_str[fen_str_i] <= '9') {
        board->halfmoves = board->halfmoves * 10 + (fen_str[fen_str_i] - '0');
        fen_str_i++;
    }

    fen_str_i++;

    board_ext->fullmoves = 0;
    while (fen_str[fen_str_i] >= '0' && fen_str[fen_str_i] <= '9') {
        board_ext->fullmoves = board_ext->fullmoves * 10 + (fen_str[fen_str_i] - '0');
        fen_str_i++;
    }
}

void board_to_fen(char * fen_str, const board_t * board, const board_ext_t * board_ext) {
    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t empty_mask = ~(white_mask | black_mask);

    int fen_str_i = 0;

    for (int y = 0; y < 8; ++y) {
        int blank = 0;

        for (int x = 0; x < 8; ++x) {
            uint64_t bit = 1ULL << (y * 8 + x);

            if (empty_mask & bit) {
                blank++;
            } else {
                if (blank > 0) {
                    fen_str[fen_str_i++] = '0' + blank;
                    blank = 0;
                }

                if (board->white_pawns & bit) {
                    fen_str[fen_str_i++] = 'P';
                } else if (board->black_pawns & bit) {
                    fen_str[fen_str_i++] = 'p';
                } else if (board->white_bishops & bit) {
                    fen_str[fen_str_i++] = 'B';
                } else if (board->black_bishops & bit) {
                    fen_str[fen_str_i++] = 'b';
                } else if (board->white_knights & bit) {
                    fen_str[fen_str_i++] = 'N';
                } else if (board->black_knights & bit) {
                    fen_str[fen_str_i++] = 'n';
                } else if (board->white_rooks & bit) {
                    fen_str[fen_str_i++] = 'R';
                } else if (board->black_rooks & bit) {
                    fen_str[fen_str_i++] = 'r';
                } else if (board->white_queens & bit) {
                    fen_str[fen_str_i++] = 'Q';
                } else if (board->black_queens & bit) {
                    fen_str[fen_str_i++] = 'q';
                } else if (board->white_kings & bit) {
                    fen_str[fen_str_i++] = 'K';
                } else if (board->black_kings & bit) {
                    fen_str[fen_str_i++] = 'k';
                }
            }
        }

        if (blank > 0) {
            fen_str[fen_str_i++] = '0' + blank;
        }

        if (y < 7) {
            fen_str[fen_str_i++] = '/';
        }
    }

    fen_str[fen_str_i++] = ' ';
    fen_str[fen_str_i++] = board->color == WHITE_COLOR ? 'w' : 'b';
    fen_str[fen_str_i++] = ' ';

    int any_can_castle = 0;
    if (board->white_right_castling) {
        fen_str[fen_str_i++] = 'K';
        any_can_castle = 1;
    }
    if (board->white_left_castling) {
        fen_str[fen_str_i++] = 'Q';
        any_can_castle = 1;
    }
    if (board->black_right_castling) {
        fen_str[fen_str_i++] = 'k';
        any_can_castle = 1;
    }
    if (board->black_left_castling) {
        fen_str[fen_str_i++] = 'q';
        any_can_castle = 1;
    }
    if (!any_can_castle) {
        fen_str[fen_str_i++] = '-';
    }

    fen_str[fen_str_i++] = ' ';

    if (board->en_passant_x == NO_EN_PASSANT) {
        fen_str[fen_str_i++] = '-';
    } else {
        fen_str[fen_str_i++] = 'a' + board->en_passant_x;
        if (board->color == WHITE_COLOR) {
            fen_str[fen_str_i++] = '6';
        } else {
            fen_str[fen_str_i++] = '3';
        }
    }

    fen_str[fen_str_i++] = ' ';

    fen_str_i += sprintf(fen_str + fen_str_i, "%d", board->halfmoves);

    fen_str[fen_str_i++] = ' ';

    fen_str_i += sprintf(fen_str + fen_str_i, "%d", board_ext->fullmoves);
}

// maximum output string length seen: 52
void board_to_short_string(char * str, const board_t * board) {
    uint64_t white_mask = board->white_pawns | board->white_knights | board->white_bishops | board->white_rooks | board->white_queens | board->white_kings;
    uint64_t black_mask = board->black_pawns | board->black_knights | board->black_bishops | board->black_rooks | board->black_queens | board->black_kings;
    uint64_t empty_mask = ~(white_mask | black_mask);

    int idx = 0;
    int blank = 0;

    for (int p = 0; p < 64; ++p) {
        uint64_t bit = 1ULL << p;

        if (empty_mask & bit) {
            blank++;
        } else {
            if (blank > 0) {
                str[idx++] = ' ' + blank;
                blank = 0;
            }

            if (board->white_pawns & bit) {
                str[idx++] = 'P';
            } else if (board->black_pawns & bit) {
                str[idx++] = 'p';
            } else if (board->white_bishops & bit) {
                str[idx++] = 'B';
            } else if (board->black_bishops & bit) {
                str[idx++] = 'b';
            } else if (board->white_knights & bit) {
                str[idx++] = 'N';
            } else if (board->black_knights & bit) {
                str[idx++] = 'n';
            } else if (board->white_rooks & bit) {
                str[idx++] = 'R';
            } else if (board->black_rooks & bit) {
                str[idx++] = 'r';
            } else if (board->white_queens & bit) {
                str[idx++] = 'Q';
            } else if (board->black_queens & bit) {
                str[idx++] = 'q';
            } else if (board->white_kings & bit) {
                str[idx++] = 'K';
            } else if (board->black_kings & bit) {
                str[idx++] = 'k';
            }
        }
    }

    if (blank > 0) {
        str[idx++] = ' ' + blank;
    }

    int castling = board->color == WHITE_COLOR ? 1 : 0;
    if (board->white_left_castling) {
        castling = (castling << 1) | 1;
    }
    if (board->white_right_castling) {
        castling = (castling << 1) | 1;
    }
    if (board->black_left_castling) {
        castling = (castling << 1) | 1;
    }
    if (board->black_right_castling) {
        castling = (castling << 1) | 1;
    }

    str[idx++] = ' ' + castling;

    if (board->en_passant_x != NO_EN_PASSANT) {
        str[idx++] = 'a' + board->en_passant_x;
    }

    str[idx] = 0;
}
