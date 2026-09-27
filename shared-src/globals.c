#include "common.h"

char buffer[INPUT_BUFFER_SIZE];

board_t board;
board_ext_t board_ext;

uint64_t white_pawn_capture_masks[64];
uint64_t black_pawn_capture_masks[64];
uint64_t white_en_passant_capture_masks[8];
uint64_t black_en_passant_capture_masks[8];
uint64_t knight_moves_masks[64];
uint64_t king_moves_masks[64];

int64_t zobrist_map[64][12];
int64_t zobrist_side_to_move; // hashes when black
int64_t zobrist_en_passant[8];
int64_t zobrist_castling[4];

hash_table_entry_t * hash_table;

int64_t search_history[MAX_GAME_PLAYS + MAX_TOTAL_SEARCH_DEPTH + 4];
int search_history_count;

struct timeval search_start;
long int search_soft_ms;
long int search_budget_ms;
int search_depth_limit = DEFAULT_SEARCH_DEPTH;
int search_aborted;
uint64_t search_nodes;
int search_abortable;

unsigned int opening_book_size;
uint64_t opening_book[MAX_SUPPORTED_OB_RULES];
char ob_play_colors[MAX_SUPPORTED_OB_RULES];
play_short_t ob_plays[MAX_SUPPORTED_OB_RULES][4];

int opening_book_enabled = 1;
int extend_uci = 0;
int arbitrate_draws = 1;
int uci_game_in_error_state = 0;
int convert_at_ob_depth = -1;
char program_dir[1024];
