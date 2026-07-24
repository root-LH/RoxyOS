#include "../include/simplefs.h"
#include "../include/ata.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/printk.h"

static simplefs_handle_t handle;

static int simplefs_find_free_entry(
    simplefs_entry_t *entry, uint32_t *sector_out, uint32_t *index_out
)
{
    uint8_t sector[512];

    for (uint32_t s = 0; s < 16; s++)
    {
        ata_read_sector(SIMPLEFS_TABLE_SECTOR + s, sector);

        simplefs_entry_t *table = (simplefs_entry_t *)sector;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (table[i].used == 0)
            {
                *entry = table[i];
                *sector_out = SIMPLEFS_TABLE_SECTOR + s;
                *index_out = i;
                return 0;
            }
        }
    }

    return -1;
}

static uint32_t simplefs_alloc_sector(void)
{
    uint8_t sector[512];
    uint32_t max = SIMPLEFS_DATA_SECTOR;

    for (uint32_t s = 0; s < 16; s++)
    {
        if (ata_read_sector(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
            return 0;

        simplefs_entry_t *table = (simplefs_entry_t *)sector;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (!table[i].used)
                continue;

            if (table[i].start_sector == 0)
                continue;

            uint32_t end = table[i].start_sector + 1;

            if (end > max)
                max = end;
        }
    }

    return max;
}

static uint32_t simplefs_next_free_sector(void)
{
    uint32_t max = SIMPLEFS_DATA_SECTOR;

    uint8_t sector[512];

    for (uint32_t s = 0; s < 16; s++)
    {
        if (ata_read_sector(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
            return 0;

        simplefs_entry_t *table =
            (simplefs_entry_t *)sector;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (!table[i].used)
                continue;

            uint32_t end =
                table[i].start_sector + 1;

            if (end > max)
                max = end;
        }
    }

    return max;
}

void simplefs_format(void)
{
    uint8_t sector[512];

    memset(sector, 0, sizeof(sector));

    simplefs_superblock_t *sb =
        (simplefs_superblock_t *)sector;

    sb->magic = SIMPLEFS_MAGIC;
    sb->version = 1;
    sb->entry_count = 0;
    sb->data_start = SIMPLEFS_DATA_SECTOR;

    ata_write_sector(SIMPLEFS_START_SECTOR, sector);

    memset(sector, 0, sizeof(sector));

    for (int i = 0; i < 16; i++)
        ata_write_sector(SIMPLEFS_TABLE_SECTOR + i, sector);
}

int simplefs_create(const char *name)
{
    if (simplefs_find(name) != NULL)
        return -1;
        
    uint8_t sector[512];

    for (uint32_t s = 0; s < 16; s++)
    {
        if (ata_read_sector(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
            return -1;

        simplefs_entry_t *table =
            (simplefs_entry_t *)sector;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (table[i].used == 0)
            {
                memset(&table[i], 0, sizeof(simplefs_entry_t));

                strcpy(table[i].name, name);

                table[i].used = 1;
                table[i].size = 0;
                table[i].start_sector = simplefs_next_free_sector();

                if (ata_write_sector(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
                    return -1;

                return 0;
            }
        }
    }

    return -1;
}

void simplefs_list(void)
{
    uint8_t sector[512];

    for (uint32_t s = 0; s < 16; s++)
    {
        ata_read_sector(SIMPLEFS_TABLE_SECTOR + s, sector);

        simplefs_entry_t *table =
            (simplefs_entry_t *)sector;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (!table[i].used)
                continue;

            printk(table[i].name);
            printk("\n");
        }
    }
}

simplefs_handle_t *simplefs_find(const char *name)
{
    uint8_t sector[512];

    for (uint32_t s = 0; s < 16; s++)
    {
        if (ata_read_sector(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
            return NULL;

        simplefs_entry_t *table =
            (simplefs_entry_t *)sector;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (!table[i].used)
                continue;

            if (strcmp(table[i].name, name) == 0)
            {
                handle.entry = table[i];
                handle.sector = SIMPLEFS_TABLE_SECTOR + s;
                handle.index = i;

                return &handle;
            }
        }
    }

    return NULL;
}

int simplefs_delete(const char *name)
{
    simplefs_handle_t *handle = simplefs_find(name);

    if (handle == NULL)
        return -1;

    uint8_t sector[512];

    if (ata_read_sector(handle->sector, sector) != 0)
        return -1;

    simplefs_entry_t *table =
        (simplefs_entry_t *)sector;

    memset(&table[handle->index], 0, sizeof(simplefs_entry_t));

    if (ata_write_sector(handle->sector, sector) != 0)
        return -1;

    return 0;
}

int simplefs_write(const char *name, const void *data, uint32_t size)
{
    simplefs_handle_t *handle = simplefs_find(name);

    if (handle == NULL)
        return -1;

    if (size > 512)
        return -1;

    if (handle->entry.start_sector == 0)
        handle->entry.start_sector = simplefs_alloc_sector();

    uint8_t buffer[512];

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, data, size);

    if (ata_write_sector(handle->entry.start_sector, buffer) != 0)
        return -1;

    handle->entry.size = size;

    uint8_t sector[512];

    if (ata_read_sector(handle->sector, sector) != 0)
        return -1;

    simplefs_entry_t *table = (simplefs_entry_t *)sector;

    table[handle->index] = handle->entry;

    if (ata_write_sector(handle->sector, sector) != 0)
        return -1;

    return 0;
}

int simplefs_read(const char *name, void *buffer, uint32_t buffer_size)
{
    simplefs_handle_t *handle = simplefs_find(name);

    if (handle == NULL)
        return -1;

    if (handle->entry.start_sector == 0)
        return -1;

    if (buffer_size < handle->entry.size)
        return -1;

    uint8_t sector[512];

    if (ata_read_sector(handle->entry.start_sector, sector) != 0)
        return -1;

    memcpy(buffer, sector, handle->entry.size);

    return handle->entry.size;
}