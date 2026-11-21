#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/*
 * Stage 5 drivers seen from user space: the RTC clock (time, gmtime,
 * mktime, strftime) and multi-sector disk transfers (whole contiguous
 * blocks go to the drive as one command; partial blocks and odd offsets
 * still go through the sector cache). Works in /tmp and cleans up.
 */

static int failures;

static void check(const char *what, int ok)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");

    if (!ok)
        failures++;
}

static void clock_tests(void)
{
    time_t now = time(NULL);
    time_t stored = 0;

    time(&stored);

    /* 2020-01-01 00:00:00 */
    check("time() is after 2020", now >= 1577836800L);
    check("time(&t) stores the result", stored >= now && stored - now <= 1);

    struct tm tm;
    gmtime_r(&now, &tm);
    check("gmtime/mktime round trip", mktime(&tm) == now);

    time_t fixed = 951782400L;      /* 2000-02-29 00:00:00, a Tuesday */
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %a %b %j", gmtime(&fixed));
    check("strftime of 2000-02-29", strcmp(buf, "2000-02-29 00:00:00 Tue Feb 060") == 0);

    fixed = 1798761599L;            /* 2026-12-31 23:59:59 */
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&fixed));
    check("strftime of 2026-12-31 23:59:59", strcmp(buf, "2026-12-31 23:59:59") == 0);

    check("strftime reports a buffer too small", strftime(buf, 4, "%Y-%m", &tm) == 0);

    unsigned int start_ms = uptime_ms();
    time_t before = time(NULL);
    msleep(2100);
    time_t after = time(NULL);
    unsigned int took = uptime_ms() - start_ms;

    check("clock advances with the timer", after - before >= 2 && after - before <= 3);
    check("msleep(2100) took about 2.1 s", took >= 2100 && took < 2400);

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&after));
    printf("  now: %s\n", buf);
}

#define FILE_SIZE (256 * 1024)
#define CHUNK     4096

static unsigned char pattern(unsigned int i, unsigned int seed)
{
    return (unsigned char)(i * 13 + i / 512 + seed);
}

static int verify(int fd, unsigned int seed_lo, unsigned int seed_hi,
                  unsigned int hi_start, unsigned int hi_end, unsigned int step)
{
    static unsigned char buf[CHUNK];

    if (lseek(fd, 0, SEEK_SET) != 0)
        return 0;

    for (unsigned int off = 0; off < FILE_SIZE; )
    {
        unsigned int want = step;

        if (want > FILE_SIZE - off)
            want = FILE_SIZE - off;

        if (read(fd, buf, want) != (int)want)
            return 0;

        for (unsigned int i = 0; i < want; i++)
        {
            unsigned int pos = off + i;
            unsigned int seed = pos >= hi_start && pos < hi_end ? seed_hi : seed_lo;

            if (buf[i] != pattern(pos, seed))
            {
                printf("  mismatch at %u\n", pos);
                return 0;
            }
        }

        off += want;
    }

    return 1;
}

static void disk_tests(void)
{
    static unsigned char buf[CHUNK];
    int fd = open("/tmp/hwtest.bin", O_RDWR | O_CREAT | O_TRUNC);
    int ok = fd >= 0;

    unsigned int start = uptime_ms();

    for (unsigned int off = 0; ok && off < FILE_SIZE; off += CHUNK)
    {
        for (unsigned int i = 0; i < CHUNK; i++)
            buf[i] = pattern(off + i, 0);

        ok = write(fd, buf, CHUNK) == CHUNK;
    }

    check("write 256 KB in 4 KB chunks", ok);
    printf("  write took %u ms\n", uptime_ms() - start);

    start = uptime_ms();
    check("read back in 4 KB chunks", ok && verify(fd, 0, 0, 0, 0, CHUNK));
    printf("  read took %u ms\n", uptime_ms() - start);

    /* Odd sizes: every read mixes partial blocks with whole ones. */
    check("read back in 3001-byte pieces", ok && verify(fd, 0, 0, 0, 0, 3001));
    check("read back in 700-byte pieces", ok && verify(fd, 0, 0, 0, 0, 700));

    /* Overwrite an unaligned middle range: partial edges + whole blocks. */
    unsigned int lo = 10000, hi = 10000 + 3 * CHUNK + 123;

    ok = ok && lseek(fd, (int)lo, SEEK_SET) == (int)lo;

    for (unsigned int off = lo; ok && off < hi; )
    {
        unsigned int n = hi - off < CHUNK ? hi - off : CHUNK;

        for (unsigned int i = 0; i < n; i++)
            buf[i] = pattern(off + i, 99);

        ok = write(fd, buf, n) == (int)n;
        off += n;
    }

    check("overwrite an unaligned range", ok);
    check("only that range changed", ok && verify(fd, 0, 99, lo, hi, 2048));

    if (fd >= 0)
        close(fd);

    check("remove the test file", unlink("/tmp/hwtest.bin") == 0);
}

int main(void)
{
    clock_tests();
    disk_tests();

    if (failures)
        printf("hwtest: %d FAILED\n", failures);
    else
        printf("hwtest: all passed\n");

    return failures;
}
