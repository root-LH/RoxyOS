#ifndef FD_H
#define FD_H

#include "types.h"

#define MAX_FD 16

typedef struct
{
    int used;

    char filename[32];

    uint32_t offset;

} fd_entry_t;

void fd_init(void);
int fd_alloc(const char *filename);
void fd_close(int fd);
const char *fd_filename(int fd);
int fd_valid(int fd);

#endif