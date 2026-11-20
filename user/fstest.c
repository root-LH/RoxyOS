#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

/*
 * Directories, paths, big and sparse files, rename, removal rules and
 * /dev. Works in /tmp/fst and removes everything it creates.
 */

static int failures;

static void check(const char *what, int ok)
{
    printf("%-40s %s\n", what, ok ? "ok" : "FAIL");

    if (!ok)
        failures++;
}

static int write_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);

    if (fd < 0)
        return -1;

    int n = write(fd, text, strlen(text));

    close(fd);

    return n == (int)strlen(text) ? 0 : -1;
}

static unsigned char pattern(unsigned int i)
{
    return (unsigned char)(i * 7 + i / 512);
}

#define BIG_SIZE (600 * 1024)   /* direct + indirect + double indirect blocks */
#define CHUNK    4096

static void big_file(void)
{
    static unsigned char buf[CHUNK];
    int fd = open("big", O_RDWR | O_CREAT | O_TRUNC);
    int ok = fd >= 0;

    for (unsigned int off = 0; ok && off < BIG_SIZE; off += CHUNK)
    {
        for (unsigned int i = 0; i < CHUNK; i++)
            buf[i] = pattern(off + i);

        ok = write(fd, buf, CHUNK) == CHUNK;
    }

    check("write 600 KB file", ok);

    struct stat st;
    check("fstat size = 600 KB", fstat(fd, &st) == 0 && st.st_size == BIG_SIZE);

    lseek(fd, 0, SEEK_SET);

    for (unsigned int off = 0; ok && off < BIG_SIZE; off += CHUNK)
    {
        ok = read(fd, buf, CHUNK) == CHUNK;

        for (unsigned int i = 0; ok && i < CHUNK; i++)
            ok = buf[i] == pattern(off + i);
    }

    check("read it back intact", ok);
    check("unlink while open fails", unlink("big") == -1);
    close(fd);
    check("unlink after close", unlink("big") == 0);
}

static void sparse_file(void)
{
    int fd = open("sparse", O_RDWR | O_CREAT | O_TRUNC);
    char c = 'x', buf[16];

    lseek(fd, 100000, SEEK_SET);
    write(fd, &c, 1);

    struct stat st;
    fstat(fd, &st);
    check("sparse file size = 100001", st.st_size == 100001);

    lseek(fd, 50000, SEEK_SET);
    memset(buf, 1, sizeof(buf));
    read(fd, buf, sizeof(buf));

    int zeros = 1;

    for (unsigned int i = 0; i < sizeof(buf); i++)
        zeros &= buf[i] == 0;

    check("hole reads as zeros", zeros);

    lseek(fd, 100000, SEEK_SET);
    check("byte after the hole", read(fd, buf, 4) == 1 && buf[0] == 'x');

    close(fd);
    unlink("sparse");
}

static int count_entries(const char *path, const char *want)
{
    DIR *dir = opendir(path);
    int found = 0, total = 0;
    struct dirent *e;

    if (!dir)
        return -1;

    while ((e = readdir(dir)))
    {
        total++;

        if (want && strcmp(e->d_name, want) == 0)
            found = 1;
    }

    closedir(dir);

    return want ? found : total;
}

int main(void)
{
    char cwd[PATH_MAX];
    struct stat st, st2;

    mkdir("/tmp/fst");
    check("chdir /tmp/fst", chdir("/tmp/fst") == 0);
    check("getcwd = /tmp/fst", getcwd(cwd, sizeof(cwd)) && strcmp(cwd, "/tmp/fst") == 0);
    check("chdir to a file fails", chdir("/bin/hello") == -1);

    check("mkdir sub", mkdir("sub") == 0);
    check("mkdir existing fails", mkdir("sub") == -1);
    check("create sub/a.txt", write_file("sub/a.txt", "alpha\n") == 0);
    check("stat via ../fst/./sub//a.txt",
          stat("../fst/./sub//a.txt", &st) == 0 && st.st_size == 6 && S_ISREG(st.st_mode));
    check("stat dir is S_IFDIR", stat("sub", &st) == 0 && S_ISDIR(st.st_mode));
    check("readdir finds a.txt", count_entries("sub", "a.txt") == 1);
    check("readdir: . .. a.txt", count_entries("sub", NULL) == 3);

    check("open dir for writing fails", open("sub", O_WRONLY) == -1);

    int fd = open("sub", O_RDONLY);
    char c;
    check("read() on a dir fails", read(fd, &c, 1) == -1);
    close(fd);

    check("name of 28 chars fails", mkdir("abcdefghijklmnopqrstuvwxyz01") == -1);
    check("name of 27 chars works", mkdir("abcdefghijklmnopqrstuvwxyz0") == 0);
    rmdir("abcdefghijklmnopqrstuvwxyz0");

    big_file();
    sparse_file();

    check("rename across dirs", rename("sub/a.txt", "b.txt") == 0);
    check("old name gone", stat("sub/a.txt", &st) == -1);
    check("new name has the data", stat("b.txt", &st) == 0 && st.st_size == 6);

    write_file("c.txt", "gamma\n");
    check("rename onto a file replaces it", rename("c.txt", "b.txt") == 0);
    check("replaced contents", stat("b.txt", &st) == 0 && st.st_size == 6 &&
                               stat("c.txt", &st2) == -1);

    mkdir("other");
    check("move a dir into another", rename("sub", "other/sub") == 0);
    check("moved dir's .. is its new parent",
          stat("other/sub/..", &st) == 0 && stat("other", &st2) == 0 && st.st_ino == st2.st_ino);
    check("dir into itself fails", rename("other", "other/sub/x") == -1);

    check("rmdir non-empty fails", rmdir("other") == -1);
    check("unlink a dir fails", unlink("other/sub") == -1);
    check("rmdir a file fails", rmdir("b.txt") == -1);
    check("rmdir empty dir", rmdir("other/sub") == 0 && rmdir("other") == 0);

    fd = open("/dev/null", O_RDWR);
    check("/dev/null swallows writes", write(fd, "xyz", 3) == 3);
    check("/dev/null reads EOF", read(fd, &c, 1) == 0);
    close(fd);

    fd = open("/dev/zero", O_RDONLY);
    c = 1;
    check("/dev/zero reads zeros", read(fd, &c, 1) == 1 && c == 0);
    close(fd);

    check("cannot create in /dev", open("/dev/new", O_WRONLY | O_CREAT) == -1);
    check("stat /dev/tty is a device", stat("/dev/tty", &st) == 0 && S_ISCHR(st.st_mode));

    unlink("b.txt");
    chdir("/");
    check("cleanup: rmdir /tmp/fst", rmdir("/tmp/fst") == 0);

    printf("%d failure(s)\n", failures);

    return failures;
}
