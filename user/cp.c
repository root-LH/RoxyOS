#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* cp from to: copies a file; a directory target means "into that directory". */
static const char *base_name(const char *path)
{
    const char *base = path;

    for (const char *p = path; *p; p++)
    {
        if (*p == '/' && p[1])
            base = p + 1;
    }

    return base;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: cp <from> <to>\n");
        return 1;
    }

    char to[PATH_MAX];
    struct stat st, src_st;

    if (stat(argv[2], &st) == 0 && st.st_mode == S_IFDIR)
        snprintf(to, sizeof(to), "%s/%s", argv[2], base_name(argv[1]));
    else
        snprintf(to, sizeof(to), "%s", argv[2]);

    int src = open(argv[1], O_RDONLY);

    if (src < 0 || fstat(src, &src_st) != 0 || src_st.st_mode != S_IFREG)
    {
        fprintf(stderr, "cp: cannot read %s\n", argv[1]);
        return 1;
    }

    /* Same inode: truncating the target would destroy the source. */
    if (stat(to, &st) == 0 && st.st_ino == src_st.st_ino)
    {
        fprintf(stderr, "cp: %s and %s are the same file\n", argv[1], to);
        return 1;
    }

    int dst = open(to, O_WRONLY | O_CREAT | O_TRUNC);

    if (dst < 0)
    {
        fprintf(stderr, "cp: cannot create %s\n", to);
        return 1;
    }

    char buf[4096];
    int n, status = 0;

    while ((n = read(src, buf, sizeof(buf))) > 0)
    {
        if (write(dst, buf, (size_t)n) != n)
        {
            fprintf(stderr, "cp: write failed (disk full?)\n");
            status = 1;
            break;
        }
    }

    if (n < 0)
    {
        fprintf(stderr, "cp: read error\n");
        status = 1;
    }

    close(src);
    close(dst);

    return status;
}
