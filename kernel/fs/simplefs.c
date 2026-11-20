#include "../include/simplefs.h"
#include "../include/bcache.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/printk.h"

static simplefs_handle_t handle;

static uint32_t simplefs_alloc_sector(uint32_t count)
{
    uint8_t used[SIMPLEFS_DATA_SECTORS] = {0};

    uint8_t sector[512];

    for (uint32_t s = 0; s < 16; s++)
    {
        if (bcache_read(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
            return 0;

        simplefs_entry_t *table = (simplefs_entry_t *)sector;

        for (uint32_t i = 0; i < 8; i++)
        {
            if (!table[i].used)
                continue;

            if (table[i].start_sector == 0)
                continue;

            uint32_t used_sectors = (table[i].size + 511) / 512;
            for (uint32_t j = 0; j < used_sectors; j++)
            {
                if (table[i].start_sector < SIMPLEFS_DATA_SECTOR)
                    continue;
                uint32_t index = table[i].start_sector - SIMPLEFS_DATA_SECTOR + j;

                if (index < sizeof(used))
                    used[index] = 1;
            }
        }
    }

    uint32_t free_count = 0;

    for (uint32_t i = 0; i < sizeof(used); i++)
    {
        if (!used[i])
        {
            free_count++;

            if (free_count == count)
            {
                return SIMPLEFS_DATA_SECTOR + i - count + 1;
            }
        }
        else
        {
            free_count = 0;
        }
    }

    return 0;
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

    bcache_write(SIMPLEFS_START_SECTOR, sector);

    memset(sector, 0, sizeof(sector));

    for (int i = 0; i < 16; i++)
        bcache_write(SIMPLEFS_TABLE_SECTOR + i, sector);
}

int simplefs_create(const char *name)
{
    if (simplefs_find(name) != NULL)
        return -1;
        
    uint8_t sector[512];

    for (uint32_t s = 0; s < 16; s++)
    {
        if (bcache_read(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
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
                table[i].start_sector = 0;

                if (bcache_write(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
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
        bcache_read(SIMPLEFS_TABLE_SECTOR + s, sector);

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
        if (bcache_read(SIMPLEFS_TABLE_SECTOR + s, sector) != 0)
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

    if (bcache_read(handle->sector, sector) != 0)
        return -1;

    simplefs_entry_t *table =
        (simplefs_entry_t *)sector;

    memset(&table[handle->index], 0, sizeof(simplefs_entry_t));

    if (bcache_write(handle->sector, sector) != 0)
        return -1;

    return 0;
}

int simplefs_write(const char *name, const void *data, uint32_t size)
{
    simplefs_handle_t *handle = simplefs_find(name);

    if (handle == NULL)
        return -1;

    uint32_t sector_count = (size + 511) / 512;
    uint32_t write_start = handle->entry.start_sector;

    if (write_start != 0)
    {
        uint32_t current_sector_count =
            (handle->entry.size + 511) / 512;

        if (sector_count > current_sector_count)
        {
            uint32_t old_start = write_start;

            handle->entry.start_sector = 0;

            write_start = simplefs_alloc_sector(sector_count);

            handle->entry.start_sector = old_start;

            if (write_start == 0)
                return -1;
        }
    }
    else
    {
        write_start = simplefs_alloc_sector(sector_count);

        if (write_start == 0)
            return -1;
    }

    for (uint32_t i = 0; i < sector_count; i++)
    {
        uint8_t sector[512];

        memset(sector, 0, sizeof(sector));

        uint32_t offset = i * 512;
        uint32_t remain = size - offset;
        uint32_t copy_size = (remain > 512) ? 512 : remain;

        memcpy(
            sector,
            (const uint8_t *)data + offset,
            copy_size);

        if (bcache_write(write_start + i, sector) != 0)
            return -1;
    }

    handle->entry.start_sector = write_start;
    handle->entry.size = size;

    uint8_t sector[512];

    if (bcache_read(handle->sector, sector) != 0)
        return -1;

    simplefs_entry_t *table = (simplefs_entry_t *)sector;

    table[handle->index] = handle->entry;

    if (bcache_write(handle->sector, sector) != 0)
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

    uint32_t sector_count = (handle->entry.size + 511) / 512;

    for (uint32_t i = 0; i < sector_count; i++)
    {
        uint8_t sector[512];

        if (bcache_read(handle->entry.start_sector + i, sector) != 0)
            return -1;

        uint32_t offset = i * 512;
        uint32_t remain = handle->entry.size - offset;
        uint32_t copy_size = (remain > 512) ? 512 : remain;

        memcpy(
            (uint8_t *)buffer + offset,
            sector,
            copy_size);
    }

    return handle->entry.size;
}

int simplefs_read_at(const char *name, uint32_t offset, void *buffer, uint32_t len)
{
    simplefs_handle_t *handle = simplefs_find(name);

    if (handle == NULL)
        return -1;

    simplefs_entry_t entry = handle->entry;

    if (offset >= entry.size)
        return 0;

    if (len > entry.size - offset)
        len = entry.size - offset;

    uint8_t sector[512];
    uint32_t done = 0;

    while (done < len)
    {
        uint32_t pos = offset + done;
        uint32_t in_sector = pos % 512;
        uint32_t chunk = 512 - in_sector;

        if (chunk > len - done)
            chunk = len - done;

        if (bcache_read(entry.start_sector + pos / 512, sector) != 0)
            return -1;

        memcpy((uint8_t *)buffer + done, sector + in_sector, chunk);

        done += chunk;
    }

    return (int)done;
}
