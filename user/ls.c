#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ls [-a] [path...]: type, size and name of each entry (dirs end in '/'). */
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

static int list(const char *path, int all, int header)
{
    struct stat st;

    if (stat(path, &st) != 0)
    {
        fprintf(stderr, "ls: %s: not found\n", path);
        return 1;
    }

    if (st.st_mode != S_IFDIR)
    {
        printf("%c %8u  %s\n", type_char(st.st_mode), st.st_size, path);
        return 0;
    }

    DIR *dir = opendir(path);

    if (!dir)
    {
        fprintf(stderr, "ls: %s: cannot open\n", path);
        return 1;
    }

    if (header)
        printf("%s:\n", path);

    struct dirent *e;
    char full[PATH_MAX];
    int slash = path[strlen(path) - 1] == '/';

    while ((e = readdir(dir)) != NULL)
    {
        if (!all && e->d_name[0] == '.')
            continue;

        unsigned int size = 0;

        snprintf(full, sizeof(full), "%s%s%s", path, slash ? "" : "/", e->d_name);

        if (stat(full, &st) == 0)
            size = st.st_size;

        printf("%c %8u  %s%s\n", type_char(e->d_type), size, e->d_name,
               e->d_type == S_IFDIR ? "/" : "");
    }

    closedir(dir);

    return 0;
}

int main(int argc, char **argv)
{
    int all = 0, first = 1, status = 0;

    if (argc > 1 && strcmp(argv[1], "-a") == 0)
        all = 1, first = 2;

    if (first >= argc)
        return list(".", all, 0);

    int many = argc - first > 1;

    for (int i = first; i < argc; i++)
    {
        status |= list(argv[i], all, many);

        if (many && i + 1 < argc)
            putchar('\n');
    }

    return status;
}
