#ifndef __UCI_H_
#define __UCI_H_

#include "common.h"

int format_play_uci(char * dest, const play_t * play);
void send_search_info(int depth, int score, const play_t * play);
void send_uci_command(FILE * fd, const char * str);
FILE * init_log_file(const char * program_name);
long int read_go_option(const char * cmd, const char * name);
void set_search_limits(const char * cmd);
void uci_mode(FILE * fd);

#endif
