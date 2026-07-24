#ifndef SIMPLEFS_H
#define SIMPLEFS_H

#include <stdint.h>

#define SIMPLEFS_START_SECTOR   128
#define SIMPLEFS_TABLE_SECTOR   129
#define SIMPLEFS_DATA_SECTOR    145

#define SIMPLEFS_MAGIC 0x53465331

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t data_start;
} simplefs_superblock_t;

#define SIMPLEFS_MAX_NAME 32

typedef struct
{
    char name[SIMPLEFS_MAX_NAME];

    uint32_t start_sector;
    uint32_t size;
    uint8_t used;
    uint8_t reserved[23];
} simplefs_entry_t;

typedef struct
{
    simplefs_entry_t entry;

    uint32_t sector;
    uint32_t index;
} simplefs_handle_t;

void simplefs_format(void);
int simplefs_create(const char *name);
void simplefs_list(void);
simplefs_handle_t *simplefs_find(const char *name);
int simplefs_delete(const char *name);
int simplefs_write(const char *name, const void *data, uint32_t size);
int simplefs_read(const char *name, void *buffer, uint32_t buffer_size);

#endif