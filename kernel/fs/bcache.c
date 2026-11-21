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

/*
 * Bulk reads skip the cache: the disk always has the newest data
 * (write-through), and a large sequential read would only push out the
 * small metadata sectors the cache is good at. Cached sectors are still
 * copied from memory; each run of uncached ones is one ATA command.
 */
int bcache_read_blocks(uint32_t lba, uint32_t count, void *buf)
{
    uint8_t *out = (uint8_t *)buf;
    int result = 0;

    kmutex_lock(&bcache_lock);

    uint32_t i = 0;

    while (i < count)
    {
        bcache_entry_t *e = find(lba + i);

        if (e)
        {
            stats.hits++;
            e->last_use = ++clock;
            memcpy(out + i * ATA_SECTOR_SIZE, e->data, ATA_SECTOR_SIZE);
            i++;
            continue;
        }

        uint32_t run = 1;

        while (i + run < count && run < ATA_MAX_SECTORS && !find(lba + i + run))
            run++;

        stats.misses += run;
        stats.bulk++;

        if (ata_read_sectors(lba + i, run, out + i * ATA_SECTOR_SIZE) != 0)
        {
            result = -1;
            break;
        }

        i += run;
    }

    kmutex_unlock(&bcache_lock);

    return result;
}

/* Bulk write straight to the disk; cached copies of those sectors are updated. */
int bcache_write_blocks(uint32_t lba, uint32_t count, const void *buf)
{
    const uint8_t *in = (const uint8_t *)buf;
    int result = 0;

    kmutex_lock(&bcache_lock);

    for (uint32_t i = 0; i < count && result == 0; i += ATA_MAX_SECTORS)
    {
        uint32_t run = count - i < ATA_MAX_SECTORS ? count - i : ATA_MAX_SECTORS;

        stats.writes += run;
        stats.bulk++;

        result = ata_write_sectors(lba + i, run, in + i * ATA_SECTOR_SIZE);
    }

    for (uint32_t i = 0; i < count; i++)
    {
        bcache_entry_t *e = find(lba + i);

        if (!e)
            continue;

        if (result == 0)
            memcpy(e->data, in + i * ATA_SECTOR_SIZE, ATA_SECTOR_SIZE);
        else
            e->valid = 0;   /* unknown after a failed write */
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
