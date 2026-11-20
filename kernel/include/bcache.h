#ifndef BCACHE_H
#define BCACHE_H

#include "types.h"

/*
 * Sector cache in front of the ATA driver: BCACHE_ENTRIES sectors, least
 * recently used replaced first. Writes go to the disk immediately
 * (write-through), so the cache never holds data the disk does not.
 */
#define BCACHE_ENTRIES 64

typedef struct
{
    uint32_t hits;
    uint32_t misses;
    uint32_t writes;
} bcache_stats_t;

/* Copies sector lba (512 bytes) into buf. Returns 0, or -1 on a disk error. */
int bcache_read(uint32_t lba, void *buf);

/* Writes buf to sector lba and keeps the copy cached. Returns 0 or -1. */
int bcache_write(uint32_t lba, const void *buf);

/* Drops every cached sector (after the disk was changed behind our back). */
void bcache_invalidate(void);

void bcache_get_stats(bcache_stats_t *stats);

#endif
