#include "common.h"

void fen_to_board(board_t * board, char * color, const char * fen_str) {
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      board->b[y * 8 + x] = ' ';
    }
  }

  int fen_str_i = 0;

  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      if (fen_str[fen_str_i] >= '1' && fen_str[fen_str_i] <= '8') {
        x += fen_str[fen_str_i] - '1';
      } else {
        board->b[y * 8 + x] = fen_str[fen_str_i];
      }
      fen_str_i++;
    }

    fen_str_i++;
  }

  if (fen_str[fen_str_i] == 'w') {
      *color = WHITE_COLOR;
  } else {
      *color = BLACK_COLOR;
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

  board->fullmoves = 0;
  while (fen_str[fen_str_i] >= '0' && fen_str[fen_str_i] <= '9') {
    board->fullmoves = board->fullmoves * 10 + (fen_str[fen_str_i] - '0');
    fen_str_i++;
  }
}

void board_to_fen(char * fen_str, const board_t * board, char color) {
  int fen_str_i = 0;

  for (int y = 0; y < 8; ++y) {
    int blank = 0;

    for (int x = 0; x < 8; ++x) {
      if (board->b[y * 8 + x] == ' ') {
        blank++;
      } else {
        if (blank > 0) {
          fen_str[fen_str_i++] = '0' + blank;
          blank = 0;
        }
        fen_str[fen_str_i++] = board->b[y * 8 + x];
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
  fen_str[fen_str_i++] = color == WHITE_COLOR ? 'w' : 'b';
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
    if (color == WHITE_COLOR) {
      fen_str[fen_str_i++] = '6';
    } else {
      fen_str[fen_str_i++] = '3';
    }
  }

  fen_str[fen_str_i++] = ' ';

  fen_str_i += sprintf(fen_str + fen_str_i, "%d", board->halfmoves);

  fen_str[fen_str_i++] = ' ';

  fen_str_i += sprintf(fen_str + fen_str_i, "%d", board->fullmoves);
}
