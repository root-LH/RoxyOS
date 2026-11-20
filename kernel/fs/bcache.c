#include "../include/bcache.h"
#include "../include/ata.h"
#include "../include/string.h"
#include "../include/wait.h"

typedef struct
{
    int valid;
    uint32_t lba;
    uint32_t last_use;      /* clock value; smallest = least recently used */
    uint8_t data[ATA_SECTOR_SIZE];
} bcache_entry_t;

static bcache_entry_t cache[BCACHE_ENTRIES];
static uint32_t clock = 0;
static bcache_stats_t stats;

/* Disk requests sleep, so the cache is guarded by a sleeping lock. */
static kmutex_t bcache_lock = KMUTEX_INIT;

static bcache_entry_t *find(uint32_t lba)
{
    for (int i = 0; i < BCACHE_ENTRIES; i++)
    {
        if (cache[i].valid && cache[i].lba == lba)
            return &cache[i];
    }

    return 0;
}

/* A free entry, or the least recently used one. */
static bcache_entry_t *victim(void)
{
    bcache_entry_t *best = &cache[0];

    for (int i = 0; i < BCACHE_ENTRIES; i++)
    {
        if (!cache[i].valid)
            return &cache[i];

        if (cache[i].last_use < best->last_use)
            best = &cache[i];
    }

    return best;
}

int bcache_read(uint32_t lba, void *buf)
{
    int result = 0;

    kmutex_lock(&bcache_lock);

    bcache_entry_t *e = find(lba);

    if (e)
    {
        stats.hits++;
    }
    else
    {
        stats.misses++;
        e = victim();
        e->valid = 0;

        if (ata_read_sector(lba, e->data) != 0)
        {
            result = -1;
            goto out;
        }

        e->valid = 1;
        e->lba = lba;
    }

    e->last_use = ++clock;
    memcpy(buf, e->data, ATA_SECTOR_SIZE);

out:
    kmutex_unlock(&bcache_lock);

    return result;
}

int bcache_write(uint32_t lba, const void *buf)
{
    kmutex_lock(&bcache_lock);

    stats.writes++;

    bcache_entry_t *e = find(lba);

    if (!e)
        e = victim();

    int result = ata_write_sector(lba, buf);

    if (result == 0)
    {
        memcpy(e->data, buf, ATA_SECTOR_SIZE);
        e->valid = 1;
        e->lba = lba;
        e->last_use = ++clock;
    }
    else
    {
        /* The sector's contents on disk are now unknown. */
        e->valid = 0;
    }

    kmutex_unlock(&bcache_lock);

    return result;
}

void bcache_invalidate(void)
{
    kmutex_lock(&bcache_lock);

    for (int i = 0; i < BCACHE_ENTRIES; i++)
        cache[i].valid = 0;

    kmutex_unlock(&bcache_lock);
}

void bcache_get_stats(bcache_stats_t *out)
{
    *out = stats;
}
