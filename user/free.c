#include <stdio.h>
#include <roxy.h>

/* free: physical memory, kernel heap and block cache statistics. */
int main(void)
{
    struct meminfo m;

    if (meminfo(&m) != 0)
    {
        fprintf(stderr, "free: cannot read memory information\n");
        return 1;
    }

    unsigned int kb = m.page_size / 1024;

    printf("memory: %u KB total, %u KB used, %u KB free (%u frames of %u KB)\n",
           m.total_frames * kb, (m.total_frames - m.free_frames) * kb,
           m.free_frames * kb, m.total_frames, kb);
    printf("heap:   %u KB mapped, %u B used, %u B free\n",
           m.heap_mapped / 1024, m.heap_used, m.heap_free);
    printf("bcache: %u sectors, %u hits, %u misses, %u writes, %u bulk transfers\n",
           m.cache_sectors, m.cache_hits, m.cache_misses, m.cache_writes, m.cache_bulk);

    return 0;
}
