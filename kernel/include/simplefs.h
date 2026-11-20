#ifndef SIMPLEFS_H
#define SIMPLEFS_H

#include <stdint.h>

/* 섹터 2~1025는 커널 영역 (boot/stage1.asm). tools/fsadd.py와 값을 맞출 것 */
#define SIMPLEFS_START_SECTOR   2048
#define SIMPLEFS_TABLE_SECTOR   (SIMPLEFS_START_SECTOR + 1)
#define SIMPLEFS_DATA_SECTOR    (SIMPLEFS_TABLE_SECTOR + 16)

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

/* Reads up to len bytes from offset. Returns the count (0 at/after EOF) or -1. */
int simplefs_read_at(const char *name, uint32_t offset, void *buffer, uint32_t len);

/* Largest file the data area can hold. */
#define SIMPLEFS_DATA_SECTORS 1024
#define SIMPLEFS_MAX_FILE_SIZE (SIMPLEFS_DATA_SECTORS * 512)

#endif