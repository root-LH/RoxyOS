#include "include/shell_fs.h"
#include "include/vfs.h"
#include "include/file.h"
#include "include/printk.h"
#include "include/string.h"
#include "include/memory.h"

#define COPY_CHUNK 4096

static char type_char(unsigned int mode)
{
    switch (mode)
    {
        case S_IFDIR: return 'd';
        case S_IFCHR: return 'c';
        case S_IFIFO: return 'p';
        default:      return '-';
    }
}

static int usage(int argc, int need, const char *text)
{
    if (argc >= need)
        return 0;

    kprintf("Usage: %s\n", text);

    return 1;
}

/* dir + "/" + name into out[PATH_MAX]. */
static int join(const char *dir, const char *name, char *out)
{
    uint32_t dlen = strlen(dir);

    if (dlen + 1 + strlen(name) >= PATH_MAX)
        return -1;

    strcpy(out, dir);

    if (dlen == 0 || dir[dlen - 1] != '/')
        strcat(out, "/");

    strcat(out, name);

    return 0;
}

static const char *basename(const char *path)
{
    const char *base = path;

    for (const char *p = path; *p; p++)
    {
        if (*p == '/' && p[1])
            base = p + 1;
    }

    return base;
}

/* "mv a dir" / "cp a dir" target dir/a. */
static int target_path(const char *src, const char *dst, char *out)
{
    struct stat st;

    if (vfs_stat(dst, &st) == 0 && st.st_mode == S_IFDIR)
        return join(dst, basename(src), out);

    if (strlen(dst) >= PATH_MAX)
        return -1;

    strcpy(out, dst);

    return 0;
}

/* argv[from..] joined by spaces. */
static void join_args(int argc, char **argv, int from, char *buf, uint32_t size)
{
    buf[0] = '\0';

    for (int i = from; i < argc; i++)
    {
        if (strlen(buf) + strlen(argv[i]) + 2 >= size)
            break;

        strcat(buf, argv[i]);

        if (i != argc - 1)
            strcat(buf, " ");
    }
}

void cmd_ls(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : ".";
    file_t *dir = file_open(path, O_RDONLY);

    if (!dir)
    {
        kprintf("ls: %s: not found\n", path);
        return;
    }

    struct stat st;
    file_stat(dir, &st);

    if (st.st_mode != S_IFDIR)
    {
        kprintf("%c %8u  %s\n", type_char(st.st_mode), st.st_size, path);
        file_close(dir);
        return;
    }

    struct dirent entry;
    char full[PATH_MAX];

    while (file_readdir(dir, &entry) == 1)
    {
        if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
            continue;

        uint32_t size = 0;

        if (join(path, entry.d_name, full) == 0 && vfs_stat(full, &st) == 0)
            size = st.st_size;

        kprintf("%c %8u  %s%s\n", type_char(entry.d_type), size, entry.d_name,
                entry.d_type == S_IFDIR ? "/" : "");
    }

    file_close(dir);
}

void cmd_cat(int argc, char **argv)
{
    if (usage(argc, 2, "cat <file>"))
        return;

    file_t *file = file_open(argv[1], O_RDONLY);

    if (!file || file->inode->type == INODE_DIR)
    {
        kprintf("cat: %s: cannot read\n", argv[1]);

        if (file)
            file_close(file);
        return;
    }

    char *buf = kmalloc(COPY_CHUNK);
    int n = 0;

    while (buf && (n = file_read(file, buf, COPY_CHUNK)) > 0)
        printk_write(buf, (uint32_t)n);

    if (n < 0)
        kprintf("\ncat: read error\n");

    kfree(buf);
    file_close(file);
}

void cmd_touch(int argc, char **argv)
{
    if (usage(argc, 2, "touch <file>"))
        return;

    file_t *file = file_open(argv[1], O_WRONLY | O_CREAT);

    if (!file)
    {
        kprintf("touch: cannot create %s\n", argv[1]);
        return;
    }

    file_close(file);
}

static void write_text(const char *cmd, const char *path, const char *text, int flags)
{
    file_t *file = file_open(path, O_WRONLY | O_CREAT | flags);

    if (!file)
    {
        kprintf("%s: cannot open %s\n", cmd, path);
        return;
    }

    uint32_t len = strlen(text);

    if (file_write(file, text, len) != (int)len || file_write(file, "\n", 1) != 1)
        kprintf("%s: write failed (disk full?)\n", cmd);

    file_close(file);
}

void cmd_write(int argc, char **argv)
{
    if (usage(argc, 3, "write <file> <text>"))
        return;

    char text[512];

    join_args(argc, argv, 2, text, sizeof(text));
    write_text("write", argv[1], text, O_TRUNC);
}

void cmd_append(int argc, char **argv)
{
    if (usage(argc, 3, "append <file> <text>"))
        return;

    char text[512];

    join_args(argc, argv, 2, text, sizeof(text));
    write_text("append", argv[1], text, O_APPEND);
}

void cmd_rm(int argc, char **argv)
{
    if (usage(argc, 2, "rm <file>"))
        return;

    if (vfs_unlink(argv[1]) != 0)
        kprintf("rm: cannot remove %s (missing, a directory, or open)\n", argv[1]);
}

void cmd_mkdir(int argc, char **argv)
{
    if (usage(argc, 2, "mkdir <dir>"))
        return;

    if (vfs_mkdir(argv[1]) != 0)
        kprintf("mkdir: cannot create %s\n", argv[1]);
}

void cmd_rmdir(int argc, char **argv)
{
    if (usage(argc, 2, "rmdir <dir>"))
        return;

    if (vfs_rmdir(argv[1]) != 0)
        kprintf("rmdir: cannot remove %s (missing, not empty, or in use)\n", argv[1]);
}

void cmd_cd(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/";

    if (vfs_chdir(path) != 0)
        kprintf("cd: %s: no such directory\n", path);
}

void cmd_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    kprintf("%s\n", vfs_getcwd());
}

void cmd_mv(int argc, char **argv)
{
    if (usage(argc, 3, "mv <from> <to>"))
        return;

    char to[PATH_MAX];

    if (target_path(argv[1], argv[2], to) != 0 || vfs_rename(argv[1], to) != 0)
        kprintf("mv: cannot move %s to %s\n", argv[1], argv[2]);
}

void cmd_cp(int argc, char **argv)
{
    if (usage(argc, 3, "cp <from> <to>"))
        return;

    char to[PATH_MAX];
    file_t *src = file_open(argv[1], O_RDONLY);
    file_t *dst = 0;
    char *buf = kmalloc(COPY_CHUNK);

    if (!src || src->inode->type != INODE_FILE || !buf)
    {
        kprintf("cp: cannot read %s\n", argv[1]);
        goto out;
    }

    if (target_path(argv[1], argv[2], to) != 0 ||
        !(dst = file_open(to, O_WRONLY | O_CREAT | O_TRUNC)))
    {
        kprintf("cp: cannot create %s\n", argv[2]);
        goto out;
    }

    if (dst->inode == src->inode)
    {
        kprintf("cp: %s and %s are the same file\n", argv[1], to);
        goto out;
    }

    int n;

    while ((n = file_read(src, buf, COPY_CHUNK)) > 0)
    {
        if (file_write(dst, buf, (uint32_t)n) != n)
        {
            kprintf("cp: write failed (disk full?)\n");
            break;
        }
    }

out:
    if (src)
        file_close(src);

    if (dst)
        file_close(dst);

    kfree(buf);
}

void cmd_stat(int argc, char **argv)
{
    if (usage(argc, 2, "stat <path>"))
        return;

    struct stat st;

    if (vfs_stat(argv[1], &st) != 0)
    {
        kprintf("stat: %s: not found\n", argv[1]);
        return;
    }

    static const char *const types[] = { "?", "file", "device", "directory", "pipe" };

    kprintf("%s: %s, %u bytes, inode %u, %u link(s)\n", argv[1],
            st.st_mode < 5 ? types[st.st_mode] : "?", st.st_size, st.st_ino, st.st_nlink);
}

void cmd_df(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    statfs_t fs;

    if (vfs_statfs("/", &fs) != 0)
    {
        kprintf("df: cannot read the file system\n");
        return;
    }

    uint32_t used = fs.total_blocks - fs.free_blocks;

    kprintf("/: %u KB total, %u KB used, %u KB free; inodes %u used, %u free\n",
            fs.total_blocks * fs.block_size / 1024, used * fs.block_size / 1024,
            fs.free_blocks * fs.block_size / 1024,
            fs.total_inodes - fs.free_inodes, fs.free_inodes);
}

void cmd_mkfs(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (vfs_format() != 0)
        kprintf("mkfs: failed (files still open?)\n");
    else
        kprintf("File system formatted.\n");
}
