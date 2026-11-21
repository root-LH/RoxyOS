#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

/*
 * Seconds since 1970-01-01 00:00. The kernel takes the date from the CMOS
 * clock, which has no time zone: whatever the machine keeps (QEMU here is
 * started with the host's local time).
 */
typedef long time_t;

struct tm
{
    int tm_sec;     /* 0~59 */
    int tm_min;     /* 0~59 */
    int tm_hour;    /* 0~23 */
    int tm_mday;    /* 1~31 */
    int tm_mon;     /* 0~11 */
    int tm_year;    /* years since 1900 */
    int tm_wday;    /* 0 = Sunday */
    int tm_yday;    /* 0~365 */
};

/* The current time (also stored in *t when t is not NULL). */
time_t time(time_t *t);

/* Splits t into a date; the result lives in a static buffer. */
struct tm *gmtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *out);

/* Back to seconds (fields out of range are not normalized). */
time_t mktime(const struct tm *tm);

/*
 * strftime with a small set of conversions: %Y %m %d %H %M %S %a %b %j %%.
 * Returns the length written (without the NUL), or 0 if it did not fit.
 */
size_t strftime(char *buf, size_t size, const char *fmt, const struct tm *tm);

#endif
