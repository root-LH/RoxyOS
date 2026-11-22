#include <stdio.h>
#include <roxy.h>

/* df [path...]: usage of the file system holding each path (default /). */
static int show(const char *path)
{
    struct statfs fs;

    if (statfs(path, &fs) != 0)
    {
        fprintf(stderr, "df: %s: cannot read the file system\n", path);
        return 1;
    }

    unsigned int used = fs.f_blocks - fs.f_bfree;

    printf("%-10s %-6s %8u %8u %8u  %3u%%   %u/%u\n", path, fs.f_type,
           fs.f_blocks * fs.f_bsize / 1024, used * fs.f_bsize / 1024,
           fs.f_bfree * fs.f_bsize / 1024,
           fs.f_blocks ? used * 100 / fs.f_blocks : 0,
           fs.f_files - fs.f_ffree, fs.f_files);

    return 0;
}

int main(int argc, char **argv)
{
    int status = 0;

    printf("PATH       TYPE    SIZE KB  USED KB  FREE KB  USE%%  INODES\n");

    if (argc < 2)
        return show("/");

    for (int i = 1; i < argc; i++)
        status |= show(argv[i]);

    return status;
}
