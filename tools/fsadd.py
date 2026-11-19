#!/usr/bin/env python3
"""Copy a host file into the SimpleFS area of a RoxyOS disk image.

Usage: fsadd.py <os.img> <name-in-fs> <host-file>

Mirrors kernel/fs/simplefs.c: superblock at sector 2048, a 16-sector file
table at 2049 (8 x 64-byte entries per sector), contiguous file data from
sector 2065 (1024 sectors max). A file with the same name is replaced.
"""

import struct
import sys

SECTOR = 512
START_SECTOR = 2048                 # keep in sync with kernel/include/simplefs.h
TABLE_SECTOR = START_SECTOR + 1
TABLE_SECTORS = 16
DATA_SECTOR = TABLE_SECTOR + TABLE_SECTORS
DATA_SECTORS = 1024
MAGIC = 0x53465331
MAX_NAME = 32

ENTRY_FMT = "<32sIIB23x"            # name, start_sector, size, used, reserved
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)
ENTRIES_PER_SECTOR = SECTOR // ENTRY_SIZE
assert ENTRY_SIZE == 64


def read_entries(img):
    entries = []
    for s in range(TABLE_SECTORS):
        img.seek((TABLE_SECTOR + s) * SECTOR)
        data = img.read(SECTOR)
        for i in range(ENTRIES_PER_SECTOR):
            raw = data[i * ENTRY_SIZE:(i + 1) * ENTRY_SIZE]
            name, start, size, used = struct.unpack(ENTRY_FMT, raw)
            entries.append({
                "name": name.split(b"\0", 1)[0],
                "start": start, "size": size, "used": used,
                "pos": (TABLE_SECTOR + s) * SECTOR + i * ENTRY_SIZE,
            })
    return entries


def find_free_run(entries, count, skip):
    used = [False] * DATA_SECTORS
    for e in entries:
        if not e["used"] or e is skip or e["start"] < DATA_SECTOR:
            continue
        first = e["start"] - DATA_SECTOR
        for j in range((e["size"] + SECTOR - 1) // SECTOR):
            if first + j < DATA_SECTORS:
                used[first + j] = True

    run = 0
    for i in range(DATA_SECTORS):
        run = 0 if used[i] else run + 1
        if run == count:
            return DATA_SECTOR + i - count + 1
    return None


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)

    img_path, name, host_path = sys.argv[1:]
    name_b = name.encode()
    if not name_b or len(name_b) >= MAX_NAME:
        sys.exit(f"fsadd: bad name '{name}' (1..{MAX_NAME - 1} bytes)")

    with open(host_path, "rb") as f:
        payload = f.read()
    if not payload:
        sys.exit(f"fsadd: {host_path} is empty")

    with open(img_path, "r+b") as img:
        img.seek(0, 2)
        if img.tell() < (DATA_SECTOR + DATA_SECTORS) * SECTOR:
            sys.exit("fsadd: image too small for SimpleFS")

        # Superblock: only write it if missing, never wipe the table.
        img.seek(START_SECTOR * SECTOR)
        if struct.unpack("<I", img.read(4))[0] != MAGIC:
            img.seek(START_SECTOR * SECTOR)
            img.write(struct.pack("<IIII", MAGIC, 1, 0, DATA_SECTOR).ljust(SECTOR, b"\0"))

        entries = read_entries(img)
        entry = next((e for e in entries if e["used"] and e["name"] == name_b), None)
        if entry is None:
            entry = next((e for e in entries if not e["used"]), None)
            if entry is None:
                sys.exit("fsadd: file table full")

        count = (len(payload) + SECTOR - 1) // SECTOR
        start = find_free_run(entries, count, skip=entry)
        if start is None:
            sys.exit(f"fsadd: no {count} contiguous free sectors")

        img.seek(start * SECTOR)
        img.write(payload.ljust(count * SECTOR, b"\0"))

        img.seek(entry["pos"])
        img.write(struct.pack(ENTRY_FMT, name_b, start, len(payload), 1))

    print(f"fsadd: {name} ({len(payload)} bytes) -> sectors {start}..{start + count - 1}")


if __name__ == "__main__":
    main()
