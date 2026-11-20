#!/usr/bin/env python3
"""Copy a host file into the RXFS file system of a RoxyOS disk image.

Usage: fsadd.py <os.img> <path-in-fs> <host-file>

Mirrors kernel/fs/rxfs.c (see kernel/include/rxfs.h for the layout). The
file system starts at sector 2048 and fills the rest of the image. If the
image holds no valid RXFS (a new image, or the old SimpleFS), it is
formatted first, with /bin, /dev and /tmp. Missing parent directories are
created and an existing file with the same path is replaced.
"""

import struct
import sys

BLOCK = 512
START_SECTOR = 2048                 # keep in sync with kernel/fs/vfs.c
MAGIC = 0x32465852                  # "RXF2"
VERSION = 1
INODE_COUNT = 512
INODE_SIZE = 64
INODES_PER_BLOCK = BLOCK // INODE_SIZE
BITS_PER_BLOCK = BLOCK * 8
ROOT_INO = 1
DIRECT = 12
PTRS = BLOCK // 4
NAME_MAX = 27
DIRENT_SIZE = 32

T_FILE = 1                          # S_IFREG
T_DIR = 3                           # S_IFDIR

SUPER_FMT = "<10I"
INODE_FMT = "<HHI14I"               # type, nlink, size, blocks[14]
DIRENT_FMT = "<I28s"
assert struct.calcsize(INODE_FMT) == INODE_SIZE
assert struct.calcsize(DIRENT_FMT) == DIRENT_SIZE


def layout(block_count):
    bitmap_blocks = (block_count + BITS_PER_BLOCK - 1) // BITS_PER_BLOCK
    inode_start = 1 + bitmap_blocks
    inode_blocks = INODE_COUNT // INODES_PER_BLOCK
    return (MAGIC, VERSION, block_count, INODE_COUNT, 1, bitmap_blocks,
            inode_start, inode_blocks, inode_start + inode_blocks, ROOT_INO)


class Inode:
    def __init__(self, ino, raw):
        fields = struct.unpack(INODE_FMT, raw)
        self.ino = ino
        self.type, self.nlink, self.size = fields[:3]
        self.blocks = list(fields[3:])


class RxFS:
    def __init__(self, img, sectors):
        self.img = img
        self.sectors = sectors
        sb = struct.unpack(SUPER_FMT, self.read(0)[:40])
        if not (sb[2] <= sectors and sb == layout(sb[2]) and sb[8] < sb[2]):
            print(f"fsadd: formatting RXFS ({sectors // 2} KB)")
            self.format()
            sb = struct.unpack(SUPER_FMT, self.read(0)[:40])
        (_, _, self.block_count, _, self.bitmap_start, self.bitmap_blocks,
         self.inode_start, _, self.data_start, _) = sb

    # -- blocks ---------------------------------------------------------

    def read(self, block):
        self.img.seek((START_SECTOR + block) * BLOCK)
        return self.img.read(BLOCK).ljust(BLOCK, b"\0")

    def write(self, block, data):
        assert len(data) == BLOCK
        self.img.seek((START_SECTOR + block) * BLOCK)
        self.img.write(data)

    def format(self):
        sb = layout(self.sectors)
        block_count, bitmap_blocks, inode_start, inode_blocks, data_start = \
            sb[2], sb[5], sb[6], sb[7], sb[8]
        used = data_start + 1                   # metadata + root directory block
        for b in range(bitmap_blocks):
            bits = bytearray(BLOCK)
            for bit in range(BITS_PER_BLOCK):
                block = b * BITS_PER_BLOCK + bit
                if block < used or block >= block_count:
                    bits[bit // 8] |= 1 << (bit % 8)
            self.write(1 + b, bytes(bits))
        for b in range(inode_blocks):
            table = bytearray(BLOCK)
            if b == 0:
                table[0:INODE_SIZE] = struct.pack(INODE_FMT, 0xFFFF, 0, 0, *[0] * 14)
                root = [data_start] + [0] * 13
                table[INODE_SIZE:2 * INODE_SIZE] = struct.pack(
                    INODE_FMT, T_DIR, 2, 2 * DIRENT_SIZE, *root)
            self.write(inode_start + b, bytes(table))
        entries = (struct.pack(DIRENT_FMT, ROOT_INO, b".") +
                   struct.pack(DIRENT_FMT, ROOT_INO, b".."))
        self.write(data_start, entries.ljust(BLOCK, b"\0"))
        self.write(0, struct.pack(SUPER_FMT, *sb).ljust(BLOCK, b"\0"))
        self.block_count, self.bitmap_start, self.data_start = block_count, 1, data_start
        self.bitmap_blocks, self.inode_start = bitmap_blocks, inode_start
        for d in (b"bin", b"dev", b"tmp"):
            self.create(self.iget(ROOT_INO), d, T_DIR)

    def set_bit(self, block, used):
        bb = self.bitmap_start + block // BITS_PER_BLOCK
        bit = block % BITS_PER_BLOCK
        bits = bytearray(self.read(bb))
        if used:
            bits[bit // 8] |= 1 << (bit % 8)
        else:
            bits[bit // 8] &= ~(1 << (bit % 8)) & 0xFF
        self.write(bb, bytes(bits))

    def balloc(self):
        for bb in range(self.bitmap_blocks):
            bits = self.read(self.bitmap_start + bb)
            for i, byte in enumerate(bits):
                if byte == 0xFF:
                    continue
                for j in range(8):
                    block = bb * BITS_PER_BLOCK + i * 8 + j
                    if not byte & (1 << j) and self.data_start <= block < self.block_count:
                        self.set_bit(block, True)
                        self.write(block, bytes(BLOCK))
                        return block
        sys.exit("fsadd: file system full")

    def bfree(self, block):
        if self.data_start <= block < self.block_count:
            self.set_bit(block, False)

    # -- inodes ---------------------------------------------------------

    def iget(self, ino):
        raw = self.read(self.inode_start + ino // INODES_PER_BLOCK)
        off = (ino % INODES_PER_BLOCK) * INODE_SIZE
        return Inode(ino, raw[off:off + INODE_SIZE])

    def iput(self, inode):
        block = self.inode_start + inode.ino // INODES_PER_BLOCK
        raw = bytearray(self.read(block))
        off = (inode.ino % INODES_PER_BLOCK) * INODE_SIZE
        raw[off:off + INODE_SIZE] = struct.pack(
            INODE_FMT, inode.type, inode.nlink, inode.size, *inode.blocks)
        self.write(block, bytes(raw))

    def ialloc(self, type_):
        for ino in range(1, INODE_COUNT):
            inode = self.iget(ino)
            if inode.type == 0:
                inode.type, inode.nlink, inode.size = type_, 1, 0
                inode.blocks = [0] * 14
                self.iput(inode)
                return inode
        sys.exit("fsadd: out of inodes")

    # -- block map ------------------------------------------------------

    def entry(self, index_block, i, alloc):
        ptrs = list(struct.unpack(f"<{PTRS}I", self.read(index_block)))
        if not ptrs[i] and alloc:
            ptrs[i] = self.balloc()
            self.write(index_block, struct.pack(f"<{PTRS}I", *ptrs))
        return ptrs[i]

    def bmap(self, inode, fb, alloc):
        if fb < DIRECT:
            slot = fb
        elif fb < DIRECT + PTRS:
            slot = DIRECT
        else:
            slot = DIRECT + 1
        if not inode.blocks[slot] and alloc:
            inode.blocks[slot] = self.balloc()
            self.iput(inode)
        top = inode.blocks[slot]
        if not top or fb < DIRECT:
            return top
        if fb < DIRECT + PTRS:
            return self.entry(top, fb - DIRECT, alloc)
        fb -= DIRECT + PTRS
        middle = self.entry(top, fb // PTRS, alloc)
        return middle and self.entry(middle, fb % PTRS, alloc)

    def free_index(self, block, depth):
        for p in struct.unpack(f"<{PTRS}I", self.read(block)):
            if p:
                if depth > 1:
                    self.free_index(p, depth - 1)
                else:
                    self.bfree(p)
        self.bfree(block)

    def truncate(self, inode):
        for b in inode.blocks[:DIRECT]:
            if b:
                self.bfree(b)
        if inode.blocks[DIRECT]:
            self.free_index(inode.blocks[DIRECT], 1)
        if inode.blocks[DIRECT + 1]:
            self.free_index(inode.blocks[DIRECT + 1], 2)
        inode.blocks, inode.size = [0] * 14, 0
        self.iput(inode)

    def write_at(self, inode, offset, data):
        pos = 0
        while pos < len(data):
            fb, inner = divmod(offset + pos, BLOCK)
            chunk = min(BLOCK - inner, len(data) - pos)
            b = self.bmap(inode, fb, True)
            block = bytearray(self.read(b))
            block[inner:inner + chunk] = data[pos:pos + chunk]
            self.write(b, bytes(block))
            pos += chunk
        if offset + len(data) > inode.size:
            inode.size = offset + len(data)
            self.iput(inode)

    def read_all(self, inode):
        out = bytearray()
        for fb in range((inode.size + BLOCK - 1) // BLOCK):
            b = self.bmap(inode, fb, False)
            out += self.read(b) if b else bytes(BLOCK)
        return bytes(out[:inode.size])

    # -- directories ----------------------------------------------------

    def entries(self, dir_inode):
        data = self.read_all(dir_inode)
        for off in range(0, len(data), DIRENT_SIZE):
            ino, name = struct.unpack(DIRENT_FMT, data[off:off + DIRENT_SIZE])
            yield off, ino, name.split(b"\0", 1)[0]

    def lookup(self, dir_inode, name):
        for _, ino, n in self.entries(dir_inode):
            if ino and n == name:
                return self.iget(ino)
        return None

    def dir_add(self, dir_inode, name, ino):
        off = next((o for o, i, _ in self.entries(dir_inode) if i == 0), dir_inode.size)
        self.write_at(dir_inode, off, struct.pack(DIRENT_FMT, ino, name))

    def create(self, parent, name, type_):
        inode = self.ialloc(type_)
        if type_ == T_DIR:
            inode.nlink = 2
            self.iput(inode)
            self.dir_add(inode, b".", inode.ino)
            self.dir_add(inode, b"..", parent.ino)
            parent.nlink += 1
            self.iput(parent)
        self.dir_add(parent, name, inode.ino)
        return inode

    def install(self, path, payload):
        parts = [p.encode() for p in path.split("/") if p]
        if not parts:
            sys.exit("fsadd: empty path")
        for p in parts:
            if len(p) > NAME_MAX or p in (b".", b".."):
                sys.exit(f"fsadd: bad name '{p.decode()}' (1..{NAME_MAX} bytes)")
        cur = self.iget(ROOT_INO)
        for p in parts[:-1]:
            nxt = self.lookup(cur, p)
            if nxt is None:
                nxt = self.create(cur, p, T_DIR)
            elif nxt.type != T_DIR:
                sys.exit(f"fsadd: {p.decode()} is not a directory")
            cur = nxt
        inode = self.lookup(cur, parts[-1])
        if inode is None:
            inode = self.create(cur, parts[-1], T_FILE)
        elif inode.type != T_FILE:
            sys.exit(f"fsadd: {path} exists and is not a file")
        else:
            self.truncate(inode)
        self.write_at(inode, 0, payload)
        return inode


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)

    img_path, fs_path, host_path = sys.argv[1:]

    with open(host_path, "rb") as f:
        payload = f.read()

    with open(img_path, "r+b") as img:
        img.seek(0, 2)
        sectors = img.tell() // BLOCK - START_SECTOR
        if sectors < 256:
            sys.exit("fsadd: image too small for RXFS")
        fs = RxFS(img, sectors)
        inode = fs.install(fs_path, payload)

    print(f"fsadd: {fs_path} ({len(payload)} bytes) -> inode {inode.ino}")


if __name__ == "__main__":
    main()
