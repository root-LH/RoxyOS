#include "../include/rxfs.h"
#include "../include/bcache.h"
#include "../include/wait.h"
#include "../include/string.h"
#include "../include/printk.h"

typedef struct
{
    fs_t fs;
    uint32_t start;         /* first sector */
    uint32_t sectors;
    rxfs_super_t sb;
    uint32_t alloc_hint;    /* where the next block search starts */
    kmutex_t lock;          /* every operation runs under it */
} rxfs_t;

/* There is one disk, so one instance. */
static rxfs_t rxfs;

static const fs_ops_t rxfs_ops;

#define RX(fs) ((rxfs_t *)(fs)->priv)

_Static_assert(sizeof(rxfs_dinode_t) == 64, "rxfs inode must be 64 bytes");
_Static_assert(sizeof(rxfs_dirent_t) == 32, "rxfs dirent must be 32 bytes");

/* ------------------------------------------------------------------ */
/* Blocks                                                              */
/* ------------------------------------------------------------------ */

static int blk_read(rxfs_t *rx, uint32_t block, void *buf)
{
    return bcache_read(rx->start + block, buf);
}

static int blk_write(rxfs_t *rx, uint32_t block, const void *buf)
{
    return bcache_write(rx->start + block, buf);
}

static int blk_zero(rxfs_t *rx, uint32_t block)
{
    uint8_t zero[RXFS_BLOCK_SIZE];

    memset(zero, 0, sizeof(zero));

    return blk_write(rx, block, zero);
}

/* Sets (used != 0) or clears block's bit. Returns its previous value, or -1. */
static int bitmap_set(rxfs_t *rx, uint32_t block, int used)
{
    uint8_t map[RXFS_BLOCK_SIZE];
    uint32_t bb = rx->sb.bitmap_start + block / RXFS_BITS_PER_BLOCK;
    uint32_t bit = block % RXFS_BITS_PER_BLOCK;

    if (blk_read(rx, bb, map) != 0)
        return -1;

    int old = (map[bit / 8] >> (bit % 8)) & 1;

    if (used)
        map[bit / 8] |= (uint8_t)(1 << (bit % 8));
    else
        map[bit / 8] &= (uint8_t)~(1 << (bit % 8));

    if (old != !!used && blk_write(rx, bb, map) != 0)
        return -1;

    return old;
}

/* A zeroed free data block, or 0 if the disk is full. */
static uint32_t balloc(rxfs_t *rx)
{
    uint8_t map[RXFS_BLOCK_SIZE];
    uint32_t count = rx->sb.block_count;
    uint32_t loaded = (uint32_t)-1;

    for (uint32_t n = 0; n < count; n++)
    {
        uint32_t block = (rx->alloc_hint + n) % count;

        if (block < rx->sb.data_start)
            continue;

        uint32_t bb = block / RXFS_BITS_PER_BLOCK;
        uint32_t bit = block % RXFS_BITS_PER_BLOCK;

        if (bb != loaded)
        {
            if (blk_read(rx, rx->sb.bitmap_start + bb, map) != 0)
                return 0;

            loaded = bb;
        }

        if (map[bit / 8] & (1 << (bit % 8)))
            continue;

        if (bitmap_set(rx, block, 1) != 0 || blk_zero(rx, block) != 0)
            return 0;

        rx->alloc_hint = block + 1;

        return block;
    }

    return 0;
}

static void bfree(rxfs_t *rx, uint32_t block)
{
    if (block >= rx->sb.data_start && block < rx->sb.block_count)
        bitmap_set(rx, block, 0);
}

/* ------------------------------------------------------------------ */
/* Inodes                                                              */
/* ------------------------------------------------------------------ */

static int dinode_io(rxfs_t *rx, uint32_t ino, rxfs_dinode_t *d, int write)
{
    uint8_t buf[RXFS_BLOCK_SIZE];
    uint32_t block = rx->sb.inode_start + ino / RXFS_INODES_PER_BLOCK;
    rxfs_dinode_t *slot = (rxfs_dinode_t *)buf + ino % RXFS_INODES_PER_BLOCK;

    if (ino == 0 || ino >= rx->sb.inode_count || blk_read(rx, block, buf) != 0)
        return -1;

    if (!write)
    {
        *d = *slot;
        return 0;
    }

    *slot = *d;

    return blk_write(rx, block, buf);
}

static int inode_store(inode_t *inode)
{
    rxfs_dinode_t d;

    d.type = (uint16_t)inode->type;
    d.nlink = inode->nlink;
    d.size = inode->size;
    memcpy(d.blocks, inode->data, sizeof(d.blocks));

    return dinode_io(RX(inode->fs), inode->ino, &d, 1);
}

/* Referenced in-memory inode for ino (loaded from disk on a cache miss). */
static int iget(rxfs_t *rx, uint32_t ino, inode_t **out)
{
    int is_new;
    inode_t *inode = inode_get(&rx->fs, ino, &is_new);

    if (!inode)
        return -1;

    if (is_new)
    {
        rxfs_dinode_t d;

        if (dinode_io(rx, ino, &d, 0) != 0 || d.type == 0)
        {
            inode_put(inode);
            return -1;
        }

        inode->type = d.type;
        inode->nlink = d.nlink;
        inode->size = d.size;
        memcpy(inode->data, d.blocks, sizeof(d.blocks));
    }

    *out = inode;

    return 0;
}

/* A free inode number, initialised on disk as an empty file of type. */
static uint32_t ialloc(rxfs_t *rx, int type)
{
    uint8_t buf[RXFS_BLOCK_SIZE];

    for (uint32_t b = 0; b < rx->sb.inode_blocks; b++)
    {
        if (blk_read(rx, rx->sb.inode_start + b, buf) != 0)
            return 0;

        rxfs_dinode_t *table = (rxfs_dinode_t *)buf;

        for (uint32_t i = 0; i < RXFS_INODES_PER_BLOCK; i++)
        {
            uint32_t ino = b * RXFS_INODES_PER_BLOCK + i;

            if (ino == 0 || table[i].type != 0)
                continue;

            memset(&table[i], 0, sizeof(rxfs_dinode_t));
            table[i].type = (uint16_t)type;
            table[i].nlink = 1;

            return blk_write(rx, rx->sb.inode_start + b, buf) == 0 ? ino : 0;
        }
    }

    return 0;
}

static void ifree(rxfs_t *rx, uint32_t ino)
{
    rxfs_dinode_t d;

    memset(&d, 0, sizeof(d));
    dinode_io(rx, ino, &d, 1);
}

/* ------------------------------------------------------------------ */
/* Block map                                                           */
/* ------------------------------------------------------------------ */

/*
 * Entry index of *slot (a pointer stored in the index block at block),
 * allocating a zeroed block for it when alloc is set and it is empty.
 */
static uint32_t index_entry(rxfs_t *rx, uint32_t block, uint32_t index, int alloc)
{
    uint32_t ptrs[RXFS_PTRS];

    if (blk_read(rx, block, ptrs) != 0)
        return 0;

    if (!ptrs[index] && alloc)
    {
        ptrs[index] = balloc(rx);

        if (!ptrs[index] || blk_write(rx, block, ptrs) != 0)
            return 0;
    }

    return ptrs[index];
}

/*
 * Disk block holding file block fb of inode, or 0 for a hole. With alloc,
 * missing data and index blocks are allocated (the inode is stored if its
 * own pointers change). Returns 0 when the disk is full.
 */
static uint32_t bmap(rxfs_t *rx, inode_t *inode, uint32_t fb, int alloc)
{
    uint32_t *slot;

    if (fb < RXFS_DIRECT)
    {
        slot = &inode->data[fb];
    }
    else if (fb < RXFS_DIRECT + RXFS_PTRS)
    {
        slot = &inode->data[RXFS_INDIRECT];
    }
    else if (fb < RXFS_MAX_BLOCKS)
    {
        slot = &inode->data[RXFS_DINDIRECT];
    }
    else
    {
        return 0;
    }

    /* The pointer held by the inode itself (data block or top index block). */
    if (!*slot && alloc)
    {
        if (!(*slot = balloc(rx)) || inode_store(inode) != 0)
            return 0;
    }

    if (!*slot || fb < RXFS_DIRECT)
        return *slot;

    if (fb < RXFS_DIRECT + RXFS_PTRS)
        return index_entry(rx, *slot, fb - RXFS_DIRECT, alloc);

    fb -= RXFS_DIRECT + RXFS_PTRS;

    uint32_t middle = index_entry(rx, *slot, fb / RXFS_PTRS, alloc);

    if (!middle)
        return 0;

    return index_entry(rx, middle, fb % RXFS_PTRS, alloc);
}

/* Frees the blocks listed in index block (depth 1: data, 2: index blocks). */
static void free_index(rxfs_t *rx, uint32_t block, int depth)
{
    uint32_t ptrs[RXFS_PTRS];

    if (blk_read(rx, block, ptrs) == 0)
    {
        for (uint32_t i = 0; i < RXFS_PTRS; i++)
        {
            if (!ptrs[i])
                continue;

            if (depth > 1)
                free_index(rx, ptrs[i], depth - 1);
            else
                bfree(rx, ptrs[i]);
        }
    }

    bfree(rx, block);
}

static int free_blocks(rxfs_t *rx, inode_t *inode)
{
    for (int i = 0; i < RXFS_DIRECT; i++)
    {
        if (inode->data[i])
            bfree(rx, inode->data[i]);
    }

    if (inode->data[RXFS_INDIRECT])
        free_index(rx, inode->data[RXFS_INDIRECT], 1);

    if (inode->data[RXFS_DINDIRECT])
        free_index(rx, inode->data[RXFS_DINDIRECT], 2);

    memset(inode->data, 0, sizeof(inode->data));
    inode->size = 0;

    return inode_store(inode);
}

/* ------------------------------------------------------------------ */
/* File data                                                           */
/* ------------------------------------------------------------------ */

/* Longest run moved by one bulk transfer (64 KB). */
#define RXFS_MAX_RUN 128

/*
 * How many of the file blocks fb, fb+1, ... (at most max) are stored in
 * disk blocks first, first+1, ... (first is fb's block). With alloc, the
 * missing ones are allocated as it goes; allocation follows alloc_hint,
 * so a file written in order usually gets consecutive blocks. The run
 * ends at the first block that is somewhere else.
 */
static uint32_t contiguous_run(rxfs_t *rx, inode_t *inode, uint32_t fb,
                               uint32_t first, uint32_t max, int alloc)
{
    uint32_t n = 1;

    if (max > RXFS_MAX_RUN)
        max = RXFS_MAX_RUN;

    while (n < max)
    {
        /* Peek first: never allocate a block the run would not use. */
        uint32_t b = bmap(rx, inode, fb + n, 0);

        if (!b && alloc)
        {
            if (rx->alloc_hint != first + n)
                break;

            b = bmap(rx, inode, fb + n, 1);
        }

        if (b != first + n)
            break;

        n++;
    }

    return n;
}

static int blk_read_run(rxfs_t *rx, uint32_t block, uint32_t n, void *buf)
{
    return bcache_read_blocks(rx->start + block, n, buf);
}

static int blk_write_run(rxfs_t *rx, uint32_t block, uint32_t n, const void *buf)
{
    return bcache_write_blocks(rx->start + block, n, buf);
}

static int data_read(rxfs_t *rx, inode_t *inode, uint32_t offset, void *buf, uint32_t len)
{
    if (offset >= inode->size)
        return 0;

    if (len > inode->size - offset)
        len = inode->size - offset;

    uint8_t block[RXFS_BLOCK_SIZE];
    uint32_t done = 0;

    while (done < len)
    {
        uint32_t pos = offset + done;
        uint32_t in = pos % RXFS_BLOCK_SIZE;
        uint32_t chunk = RXFS_BLOCK_SIZE - in;

        if (chunk > len - done)
            chunk = len - done;

        uint32_t b = bmap(rx, inode, pos / RXFS_BLOCK_SIZE, 0);

        /* Whole blocks that sit next to each other on disk: one transfer. */
        if (b && in == 0 && len - done >= RXFS_BLOCK_SIZE)
        {
            uint32_t n = contiguous_run(rx, inode, pos / RXFS_BLOCK_SIZE, b,
                                        (len - done) / RXFS_BLOCK_SIZE, 0);

            if (n > 1)
            {
                if (blk_read_run(rx, b, n, (uint8_t *)buf + done) != 0)
                    return -1;

                done += n * RXFS_BLOCK_SIZE;
                continue;
            }
        }

        if (!b)
            memset(block, 0, sizeof(block));        /* hole */
        else if (blk_read(rx, b, block) != 0)
            return -1;

        memcpy((uint8_t *)buf + done, block + in, chunk);
        done += chunk;
    }

    return (int)done;
}

static int data_write(rxfs_t *rx, inode_t *inode, uint32_t offset,
                      const void *buf, uint32_t len)
{
    if (offset + len < offset || offset + len > RXFS_MAX_FILE_SIZE)
        return -1;

    uint8_t block[RXFS_BLOCK_SIZE];
    uint32_t done = 0;

    while (done < len)
    {
        uint32_t pos = offset + done;
        uint32_t in = pos % RXFS_BLOCK_SIZE;
        uint32_t chunk = RXFS_BLOCK_SIZE - in;

        if (chunk > len - done)
            chunk = len - done;

        uint32_t b = bmap(rx, inode, pos / RXFS_BLOCK_SIZE, 1);

        if (!b)
            break;      /* disk full: report what was written */

        if (in == 0 && len - done >= RXFS_BLOCK_SIZE)
        {
            uint32_t n = contiguous_run(rx, inode, pos / RXFS_BLOCK_SIZE, b,
                                        (len - done) / RXFS_BLOCK_SIZE, 1);

            if (n > 1)
            {
                if (blk_write_run(rx, b, n, (const uint8_t *)buf + done) != 0)
                    break;

                done += n * RXFS_BLOCK_SIZE;
                continue;
            }
        }

        /* Partial block: keep the bytes around the new ones. */
        if (chunk < RXFS_BLOCK_SIZE && blk_read(rx, b, block) != 0)
            break;

        memcpy(block + in, (const uint8_t *)buf + done, chunk);

        if (blk_write(rx, b, block) != 0)
            break;

        done += chunk;
    }

    if (offset + done > inode->size)
    {
        inode->size = offset + done;

        if (inode_store(inode) != 0)
            return -1;
    }

    return done > 0 ? (int)done : (len > 0 ? -1 : 0);
}

/* ------------------------------------------------------------------ */
/* Directories                                                         */
/* ------------------------------------------------------------------ */

#define DIRENT_SIZE sizeof(rxfs_dirent_t)

/* Finds name in dir: *ino and the entry's byte offset. Returns 0 or -1. */
static int dir_find(rxfs_t *rx, inode_t *dir, const char *name,
                    uint32_t *ino, uint32_t *offset)
{
    rxfs_dirent_t entries[RXFS_BLOCK_SIZE / DIRENT_SIZE];

    for (uint32_t off = 0; off < dir->size; off += sizeof(entries))
    {
        int n = data_read(rx, dir, off, entries, sizeof(entries));

        if (n <= 0)
            return -1;

        for (uint32_t i = 0; i < (uint32_t)n / DIRENT_SIZE; i++)
        {
            if (entries[i].ino && strcmp(entries[i].name, name) == 0)
            {
                *ino = entries[i].ino;

                if (offset)
                    *offset = off + i * DIRENT_SIZE;

                return 0;
            }
        }
    }

    return -1;
}

/* Adds { ino, name } in the first free slot, or at the end. */
static int dir_add(rxfs_t *rx, inode_t *dir, const char *name, uint32_t ino)
{
    rxfs_dirent_t entry;
    uint32_t off;

    for (off = 0; off < dir->size; off += DIRENT_SIZE)
    {
        if (data_read(rx, dir, off, &entry, DIRENT_SIZE) != DIRENT_SIZE)
            return -1;

        if (entry.ino == 0)
            break;
    }

    memset(&entry, 0, sizeof(entry));
    entry.ino = ino;
    strcpy(entry.name, name);

    return data_write(rx, dir, off, &entry, DIRENT_SIZE) == DIRENT_SIZE ? 0 : -1;
}

static int dir_set(rxfs_t *rx, inode_t *dir, uint32_t offset, uint32_t ino, const char *name)
{
    rxfs_dirent_t entry;

    memset(&entry, 0, sizeof(entry));
    entry.ino = ino;

    if (name)
        strcpy(entry.name, name);

    return data_write(rx, dir, offset, &entry, DIRENT_SIZE) == DIRENT_SIZE ? 0 : -1;
}

/* 1 if dir holds nothing but "." and "..". */
static int dir_empty(rxfs_t *rx, inode_t *dir)
{
    rxfs_dirent_t entry;

    for (uint32_t off = 2 * DIRENT_SIZE; off < dir->size; off += DIRENT_SIZE)
    {
        if (data_read(rx, dir, off, &entry, DIRENT_SIZE) != DIRENT_SIZE)
            return 0;

        if (entry.ino)
            return 0;
    }

    return 1;
}

static int valid_name(const char *name)
{
    uint32_t len = strlen(name);

    if (len == 0 || len > NAME_MAX)
        return 0;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;

    return 1;
}

/* ------------------------------------------------------------------ */
/* fs_ops                                                              */
/* ------------------------------------------------------------------ */

static int rx_lookup(inode_t *dir, const char *name, inode_t **out)
{
    rxfs_t *rx = RX(dir->fs);
    uint32_t ino;

    kmutex_lock(&rx->lock);

    int r = dir_find(rx, dir, name, &ino, 0);

    if (r == 0)
        r = iget(rx, ino, out);

    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_create(inode_t *dir, const char *name, int type, inode_t **out)
{
    rxfs_t *rx = RX(dir->fs);
    uint32_t existing;
    inode_t *inode = 0;
    int r = -1;

    if (!valid_name(name) || (type != INODE_FILE && type != INODE_DIR))
        return -1;

    kmutex_lock(&rx->lock);

    if (dir_find(rx, dir, name, &existing, 0) == 0)
        goto out;

    uint32_t ino = ialloc(rx, type);

    if (!ino)
        goto out;

    if (iget(rx, ino, &inode) != 0)
    {
        ifree(rx, ino);
        goto out;
    }

    if (type == INODE_DIR)
    {
        inode->nlink = 2;

        if (dir_add(rx, inode, ".", ino) != 0 || dir_add(rx, inode, "..", dir->ino) != 0)
            goto undo;
    }

    if (dir_add(rx, dir, name, ino) != 0)
        goto undo;

    if (type == INODE_DIR)
    {
        dir->nlink++;
        inode_store(dir);
    }

    *out = inode;
    r = 0;
    goto out;

undo:
    free_blocks(rx, inode);
    ifree(rx, ino);
    inode_put(inode);

out:
    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_remove(inode_t *dir, const char *name, int dir_only)
{
    rxfs_t *rx = RX(dir->fs);
    uint32_t ino, offset;
    inode_t *inode;
    int r = -1;

    if (!valid_name(name))
        return -1;

    kmutex_lock(&rx->lock);

    if (dir_find(rx, dir, name, &ino, &offset) != 0 || iget(rx, ino, &inode) != 0)
        goto out;

    int is_dir = inode->type == INODE_DIR;

    /*
     * Open files (and directories being walked) cannot be removed: the
     * inode cache never does I/O, so there is nobody to free them later.
     */
    if (is_dir != !!dir_only || inode->refcount > 1 || (is_dir && !dir_empty(rx, inode)))
    {
        inode_put(inode);
        goto out;
    }

    if (dir_set(rx, dir, offset, 0, 0) != 0)
    {
        inode_put(inode);
        goto out;
    }

    free_blocks(rx, inode);
    ifree(rx, ino);
    inode_put(inode);

    if (is_dir)
    {
        dir->nlink--;
        inode_store(dir);
    }

    r = 0;

out:
    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_rename(inode_t *old_dir, const char *old_name,
                     inode_t *new_dir, const char *new_name)
{
    rxfs_t *rx = RX(old_dir->fs);
    uint32_t ino, offset, target;
    uint32_t target_offset;
    inode_t *inode = 0;
    int r = -1;

    if (!valid_name(old_name) || !valid_name(new_name))
        return -1;

    kmutex_lock(&rx->lock);

    if (dir_find(rx, old_dir, old_name, &ino, &offset) != 0 || iget(rx, ino, &inode) != 0)
        goto out;

    if (dir_find(rx, new_dir, new_name, &target, &target_offset) == 0)
    {
        if (target == ino)
        {
            r = 0;      /* renaming onto itself */
            goto out;
        }

        /* Replace an existing file that nobody has open; never a directory. */
        inode_t *victim;

        if (iget(rx, target, &victim) != 0)
            goto out;

        int ok = victim->type == INODE_FILE && victim->refcount == 1 &&
                 inode->type == INODE_FILE;

        if (ok && dir_set(rx, new_dir, target_offset, ino, new_name) == 0)
        {
            free_blocks(rx, victim);
            ifree(rx, target);
            inode_put(victim);

            r = dir_set(rx, old_dir, offset, 0, 0);
        }
        else
        {
            inode_put(victim);
        }

        goto out;
    }

    if (dir_add(rx, new_dir, new_name, ino) != 0)
        goto out;

    if (dir_set(rx, old_dir, offset, 0, 0) != 0)
        goto out;

    /* A moved directory's ".." (its second entry) now names new_dir. */
    if (inode->type == INODE_DIR && old_dir != new_dir)
    {
        dir_set(rx, inode, DIRENT_SIZE, new_dir->ino, "..");

        old_dir->nlink--;
        new_dir->nlink++;
        inode_store(old_dir);
        inode_store(new_dir);
    }

    r = 0;

out:
    inode_put(inode);
    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_readdir(inode_t *dir, uint32_t *pos, struct dirent *out)
{
    rxfs_t *rx = RX(dir->fs);
    rxfs_dirent_t entry;
    int r = 0;

    kmutex_lock(&rx->lock);

    /* *pos is a byte offset into the directory, kept on an entry boundary. */
    *pos -= *pos % DIRENT_SIZE;

    while (*pos < dir->size)
    {
        if (data_read(rx, dir, *pos, &entry, DIRENT_SIZE) != DIRENT_SIZE)
        {
            r = -1;
            break;
        }

        *pos += DIRENT_SIZE;

        if (!entry.ino)
            continue;

        rxfs_dinode_t d;

        out->d_ino = entry.ino;
        out->d_type = dinode_io(rx, entry.ino, &d, 0) == 0 ? d.type : 0;
        strcpy(out->d_name, entry.name);

        r = 1;
        break;
    }

    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_read(inode_t *inode, uint32_t offset, void *buf, uint32_t len)
{
    rxfs_t *rx = RX(inode->fs);

    kmutex_lock(&rx->lock);
    int r = data_read(rx, inode, offset, buf, len);
    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_write(inode_t *inode, uint32_t offset, const void *buf, uint32_t len)
{
    rxfs_t *rx = RX(inode->fs);

    if (inode->type != INODE_FILE)
        return -1;

    kmutex_lock(&rx->lock);
    int r = data_write(rx, inode, offset, buf, len);
    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_truncate(inode_t *inode)
{
    rxfs_t *rx = RX(inode->fs);

    kmutex_lock(&rx->lock);
    int r = free_blocks(rx, inode);
    kmutex_unlock(&rx->lock);

    return r;
}

static int rx_statfs(fs_t *fs, statfs_t *out)
{
    rxfs_t *rx = RX(fs);
    uint8_t buf[RXFS_BLOCK_SIZE];

    kmutex_lock(&rx->lock);

    out->block_size = RXFS_BLOCK_SIZE;
    out->total_blocks = rx->sb.block_count;
    out->total_inodes = rx->sb.inode_count - 1;
    out->free_blocks = 0;
    out->free_inodes = 0;

    for (uint32_t block = 0; block < rx->sb.block_count; block++)
    {
        uint32_t bit = block % RXFS_BITS_PER_BLOCK;

        if (bit == 0 && blk_read(rx, rx->sb.bitmap_start + block / RXFS_BITS_PER_BLOCK, buf) != 0)
            break;

        if (!(buf[bit / 8] & (1 << (bit % 8))))
            out->free_blocks++;
    }

    for (uint32_t b = 0; b < rx->sb.inode_blocks; b++)
    {
        if (blk_read(rx, rx->sb.inode_start + b, buf) != 0)
            break;

        rxfs_dinode_t *table = (rxfs_dinode_t *)buf;

        for (uint32_t i = 0; i < RXFS_INODES_PER_BLOCK; i++)
        {
            if (b * RXFS_INODES_PER_BLOCK + i != 0 && table[i].type == 0)
                out->free_inodes++;
        }
    }

    kmutex_unlock(&rx->lock);

    return 0;
}

static const fs_ops_t rxfs_ops =
{
    .lookup   = rx_lookup,
    .create   = rx_create,
    .remove   = rx_remove,
    .rename   = rx_rename,
    .readdir  = rx_readdir,
    .read     = rx_read,
    .write    = rx_write,
    .truncate = rx_truncate,
    .statfs   = rx_statfs,
};

/* ------------------------------------------------------------------ */
/* Format / mount                                                      */
/* ------------------------------------------------------------------ */

/* The layout mkfs gives a file system of block_count blocks. */
static void layout(rxfs_super_t *sb, uint32_t block_count)
{
    memset(sb, 0, sizeof(*sb));

    sb->magic = RXFS_MAGIC;
    sb->version = RXFS_VERSION;
    sb->block_count = block_count;
    sb->inode_count = RXFS_INODE_COUNT;
    sb->bitmap_start = 1;
    sb->bitmap_blocks = (block_count + RXFS_BITS_PER_BLOCK - 1) / RXFS_BITS_PER_BLOCK;
    sb->inode_start = sb->bitmap_start + sb->bitmap_blocks;
    sb->inode_blocks = RXFS_INODE_COUNT / RXFS_INODES_PER_BLOCK;
    sb->data_start = sb->inode_start + sb->inode_blocks;
    sb->root_ino = RXFS_ROOT_INO;
}

static int valid_super(const rxfs_super_t *sb, uint32_t sectors)
{
    rxfs_super_t expect;

    if (sb->magic != RXFS_MAGIC || sb->version != RXFS_VERSION)
        return 0;

    if (sb->block_count > sectors)
        return 0;

    /* Every field must match what mkfs would have written. */
    layout(&expect, sb->block_count);

    return memcmp(sb, &expect, sizeof(expect)) == 0 &&
           sb->data_start < sb->block_count;
}

int rxfs_format(uint32_t start_lba, uint32_t sectors)
{
    rxfs_t *rx = &rxfs;
    rxfs_super_t sb;
    uint8_t buf[RXFS_BLOCK_SIZE];

    layout(&sb, sectors);

    /* Room for the metadata, the root directory and some data. */
    if (sectors < sb.data_start + 16)
        return -1;

    rx->start = start_lba;
    rx->sb = sb;

    /* Bitmap: metadata and the root directory block are in use. */
    uint32_t used = sb.data_start + 1;

    for (uint32_t b = 0; b < sb.bitmap_blocks; b++)
    {
        memset(buf, 0, sizeof(buf));

        for (uint32_t bit = 0; bit < RXFS_BITS_PER_BLOCK; bit++)
        {
            uint32_t block = b * RXFS_BITS_PER_BLOCK + bit;

            /* Bits past the end of the disk are marked used too. */
            if (block < used || block >= sb.block_count)
                buf[bit / 8] |= (uint8_t)(1 << (bit % 8));
        }

        if (blk_write(rx, sb.bitmap_start + b, buf) != 0)
            return -1;
    }

    for (uint32_t b = 0; b < sb.inode_blocks; b++)
    {
        memset(buf, 0, sizeof(buf));

        rxfs_dinode_t *table = (rxfs_dinode_t *)buf;

        if (b == 0)
        {
            table[0].type = 0xFFFF;     /* inode 0: reserved */

            table[RXFS_ROOT_INO].type = INODE_DIR;
            table[RXFS_ROOT_INO].nlink = 2;
            table[RXFS_ROOT_INO].size = 2 * DIRENT_SIZE;
            table[RXFS_ROOT_INO].blocks[0] = sb.data_start;
        }

        if (blk_write(rx, sb.inode_start + b, buf) != 0)
            return -1;
    }

    /* Root directory: "." and ".." both name the root. */
    memset(buf, 0, sizeof(buf));

    rxfs_dirent_t *root = (rxfs_dirent_t *)buf;

    root[0].ino = RXFS_ROOT_INO;
    strcpy(root[0].name, ".");
    root[1].ino = RXFS_ROOT_INO;
    strcpy(root[1].name, "..");

    if (blk_write(rx, sb.data_start, buf) != 0)
        return -1;

    /* The superblock goes last: a half-written file system stays invalid. */
    memset(buf, 0, sizeof(buf));
    memcpy(buf, &sb, sizeof(sb));

    if (blk_write(rx, 0, buf) != 0)
        return -1;

    klog("[rxfs] formatted %u blocks, %u inodes, data from block %u\n",
         sb.block_count, sb.inode_count, sb.data_start);

    return 0;
}

fs_t *rxfs_mount(uint32_t start_lba, uint32_t sectors)
{
    rxfs_t *rx = &rxfs;
    uint8_t buf[RXFS_BLOCK_SIZE];

    rx->start = start_lba;
    rx->sectors = sectors;

    if (sectors == 0 || blk_read(rx, 0, buf) != 0)
        return 0;

    memcpy(&rx->sb, buf, sizeof(rx->sb));

    if (!valid_super(&rx->sb, sectors))
    {
        klog("[rxfs] bad superblock at LBA %u (magic %08x)\n", start_lba, rx->sb.magic);
        return 0;
    }

    rx->fs.type = "rxfs";
    rx->fs.ops = &rxfs_ops;
    rx->fs.priv = rx;
    rx->alloc_hint = rx->sb.data_start;
    rx->lock = (kmutex_t)KMUTEX_INIT;

    inode_t *root = 0;

    if (iget(rx, rx->sb.root_ino, &root) != 0 || root->type != INODE_DIR)
    {
        klog("[rxfs] root inode is not a directory\n");
        inode_put(root);
        return 0;
    }

    /* The file system keeps this reference for as long as it is mounted. */
    rx->fs.root = root;

    klog("[rxfs] mounted: %u blocks (%u KB), %u inodes\n",
         rx->sb.block_count, rx->sb.block_count / 2, rx->sb.inode_count);

    return &rx->fs;
}

int rxfs_reformat(fs_t *fs)
{
    rxfs_t *rx = RX(fs);

    kmutex_lock(&rx->lock);

    int r = rxfs_format(rx->start, rx->sectors);

    if (r == 0)
    {
        /* Reload the root from the fresh table. */
        inode_t *root = fs->root;
        rxfs_dinode_t d;

        r = dinode_io(rx, RXFS_ROOT_INO, &d, 0);

        root->type = d.type;
        root->nlink = d.nlink;
        root->size = d.size;
        memcpy(root->data, d.blocks, sizeof(d.blocks));

        rx->alloc_hint = rx->sb.data_start;
    }

    kmutex_unlock(&rx->lock);

    return r;
}
