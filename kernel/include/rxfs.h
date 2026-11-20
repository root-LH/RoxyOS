#ifndef RXFS_H
#define RXFS_H

#include "types.h"
#include "vfs.h"

/*
 * RXFS: the on-disk file system (tools/fsadd.py implements the same
 * layout on the host; keep the two in sync).
 *
 * 512-byte blocks, numbered from the first sector of the file system:
 *
 *   0                          superblock
 *   1 .. bitmap_blocks         block bitmap (1 bit per block, 1 = used)
 *   inode_start ..             inode table (8 x 64-byte inodes per block)
 *   data_start ..              file and directory data, index blocks
 *
 * Inode 0 is reserved, inode 1 is the root directory. A file has 12
 * direct blocks, one indirect block (128 more) and one double indirect
 * block (128 x 128 more): up to about 8 MB. Unallocated blocks inside a
 * file read as zeros. A directory is a file of 32-byte entries
 * { inode, name[28] }, inode 0 marking a free slot; "." and ".." come first.
 */

#define RXFS_MAGIC          0x32465852      /* "RXF2" */
#define RXFS_VERSION        1
#define RXFS_BLOCK_SIZE     512
#define RXFS_INODE_COUNT    512
#define RXFS_INODES_PER_BLOCK (RXFS_BLOCK_SIZE / sizeof(rxfs_dinode_t))
#define RXFS_BITS_PER_BLOCK (RXFS_BLOCK_SIZE * 8)
#define RXFS_ROOT_INO       1

#define RXFS_DIRECT         12
#define RXFS_INDIRECT       RXFS_DIRECT             /* data[] index */
#define RXFS_DINDIRECT      (RXFS_DIRECT + 1)
#define RXFS_PTRS           (RXFS_BLOCK_SIZE / 4)   /* pointers per index block */
#define RXFS_MAX_BLOCKS     (RXFS_DIRECT + RXFS_PTRS + RXFS_PTRS * RXFS_PTRS)
#define RXFS_MAX_FILE_SIZE  ((uint32_t)RXFS_MAX_BLOCKS * RXFS_BLOCK_SIZE)

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t bitmap_start;
    uint32_t bitmap_blocks;
    uint32_t inode_start;
    uint32_t inode_blocks;
    uint32_t data_start;
    uint32_t root_ino;
} rxfs_super_t;

typedef struct
{
    uint16_t type;          /* 0 = free, else INODE_* */
    uint16_t nlink;
    uint32_t size;
    uint32_t blocks[RXFS_DIRECT + 2];   /* direct, indirect, double indirect */
} rxfs_dinode_t;

typedef struct
{
    uint32_t ino;
    char name[NAME_MAX + 1];
} rxfs_dirent_t;

/*
 * Checks the superblock and layout of the file system at start_lba
 * (sectors long). Returns the mounted file system, or 0 if it is invalid.
 */
fs_t *rxfs_mount(uint32_t start_lba, uint32_t sectors);

/* Writes an empty file system (root directory only). Returns 0 or -1. */
int rxfs_format(uint32_t start_lba, uint32_t sectors);

/* Formats a mounted rxfs in place (no other inode may be in use). */
int rxfs_reformat(fs_t *fs);

#endif
