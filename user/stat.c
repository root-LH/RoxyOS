#include <stdio.h>
#include <sys/stat.h>

/* stat path...: type, size, inode and link count. */
int main(int argc, char **argv)
{
    static const char *const types[] = { "?", "file", "device", "directory", "pipe" };
    int status = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: stat <path>...\n");
        return 1;
    }

    for (int i = 1; i < argc; i++)
    {
        struct stat st;

        if (stat(argv[i], &st) != 0)
        {
            fprintf(stderr, "stat: %s: not found\n", argv[i]);
            status = 1;
            continue;
        }

        printf("%s: %s, %u bytes, inode %u, %u link(s)\n", argv[i],
               st.st_mode < 5 ? types[st.st_mode] : "?", st.st_size, st.st_ino,
               st.st_nlink);
    }

    return status;
}
