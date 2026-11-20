#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* File descriptors: offsets, lseek, O_APPEND / O_TRUNC, stat, stdin. */

static int failures;

static void check(const char *what, int ok)
{
    printf("%-28s %s\n", what, ok ? "ok" : "FAIL");

    if (!ok)
        failures++;
}

int main(void)
{
    char buf[64];

    int fd = open("ftest", O_RDWR | O_CREAT | O_TRUNC);
    check("open(O_CREAT)", fd >= 3);

    check("write 11 bytes", write(fd, "hello world", 11) == 11);
    check("lseek(6, SEEK_SET)", lseek(fd, 6, SEEK_SET) == 6);
    check("overwrite in the middle", write(fd, "RoxyOS", 6) == 6);
    check("lseek(0, SEEK_END) = 12", lseek(fd, 0, SEEK_END) == 12);

    lseek(fd, 0, SEEK_SET);

    int n = read(fd, buf, 5);
    buf[n > 0 ? n : 0] = '\0';
    check("read 5 = \"hello\"", n == 5 && strcmp(buf, "hello") == 0);

    n = read(fd, buf, sizeof(buf) - 1);
    buf[n > 0 ? n : 0] = '\0';
    check("read rest = \" RoxyOS\"", strcmp(buf, " RoxyOS") == 0);
    check("read at EOF = 0", read(fd, buf, sizeof(buf)) == 0);
    check("close", close(fd) == 0);
    check("close twice fails", close(fd) == -1);

    fd = open("ftest", O_WRONLY | O_APPEND);
    write(fd, "!", 1);
    check("read on O_WRONLY fails", read(fd, buf, 1) == -1);
    close(fd);

    struct stat st;
    check("stat size = 13", stat("ftest", &st) == 0 && st.st_size == 13);
    check("stat missing fails", stat("nope", &st) == -1);
    check("open missing fails", open("nope", O_RDONLY) == -1);
    check("write to bad fd fails", write(9, "x", 1) == -1);
    check("lseek on console fails", lseek(1, 0, SEEK_SET) == -1);

    fd = open("ftest", O_RDWR | O_TRUNC);
    check("O_TRUNC empties the file", stat("ftest", &st) == 0 && st.st_size == 0);
    close(fd);

    printf("type a line: ");

    if (fgets(buf, sizeof(buf), stdin))
    {
        char *newline = strchr(buf, '\n');

        if (newline)
            *newline = '\0';

        printf("got [%s]\n", buf);
    }

    printf("%d failure(s)\n", failures);

    return failures;
}
