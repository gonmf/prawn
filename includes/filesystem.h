#ifndef __FILESYSTEM_H_
#define __FILESYSTEM_H_

#include "common.h"

void set_program_dir(const char * program_name);
FILE * open_data_file(const char * name, const char * mode);

#endif
