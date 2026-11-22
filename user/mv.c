#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* mv from to: renames; a directory target means "into that directory". */
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
        fprintf(stderr, "Usage: mv <from> <to>\n");
        return 1;
    }

    char to[PATH_MAX];
    struct stat st;

    if (stat(argv[2], &st) == 0 && st.st_mode == S_IFDIR)
        snprintf(to, sizeof(to), "%s/%s", argv[2], base_name(argv[1]));
    else
        snprintf(to, sizeof(to), "%s", argv[2]);

    if (rename(argv[1], to) != 0)
    {
        fprintf(stderr, "mv: cannot move %s to %s\n", argv[1], to);
        return 1;
    }

    return 0;
}
