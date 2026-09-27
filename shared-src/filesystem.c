#include "filesystem.h"

void set_program_dir(const char * program_name) {
    const char * slash = strrchr(program_name, '/');
    if (slash == NULL) {
        program_dir[0] = 0;
        return;
    }

    size_t len = (size_t)(slash - program_name) + 1;
    if (len >= sizeof(program_dir)) {
        len = sizeof(program_dir) - 1;
    }

    memcpy(program_dir, program_name, len);
    program_dir[len] = 0;
}

FILE * open_data_file(const char * name, const char * mode) {
    if (program_dir[0] != 0) {
        char path[2048];
        snprintf(path, sizeof(path), "%s%s", program_dir, name);

        FILE * fp = fopen(path, mode);
        if (fp != NULL) {
            return fp;
        }
    }

    return fopen(name, mode);
}
