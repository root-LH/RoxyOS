#include <time.h>
#include <stdio.h>

long sys_clock(void);

time_t time(time_t *t)
{
    time_t now = sys_clock();

    if (t)
        *t = now;

    return now;
}

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    return month == 1 && is_leap(year) ? 29 : days[month];
}

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    unsigned long secs = (unsigned long)*t;
    unsigned long days = secs / 86400;
    unsigned long rest = secs % 86400;

    out->tm_hour = (int)(rest / 3600);
    out->tm_min = (int)(rest / 60 % 60);
    out->tm_sec = (int)(rest % 60);

    /* 1970-01-01 was a Thursday. */
    out->tm_wday = (int)((days + 4) % 7);

    int year = 1970;

    while (days >= (unsigned long)(is_leap(year) ? 366 : 365))
    {
        days -= is_leap(year) ? 366 : 365;
        year++;
    }

    out->tm_year = year - 1900;
    out->tm_yday = (int)days;

    int month = 0;

    while (days >= (unsigned long)days_in_month(year, month))
    {
        days -= days_in_month(year, month);
        month++;
    }

    out->tm_mon = month;
    out->tm_mday = (int)days + 1;

    return out;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm buf;

    return gmtime_r(t, &buf);
}

time_t mktime(const struct tm *tm)
{
    int year = tm->tm_year + 1900;
    long days = 0;

    for (int y = 1970; y < year; y++)
        days += is_leap(y) ? 366 : 365;

    for (int m = 0; m < tm->tm_mon; m++)
        days += days_in_month(year, m);

    days += tm->tm_mday - 1;

    return days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
}

static const char *const day_names[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const month_names[12] =
{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

size_t strftime(char *buf, size_t size, const char *fmt, const struct tm *tm)
{
    size_t n = 0;

    for (; *fmt; fmt++)
    {
        char tmp[16];
        const char *piece = tmp;

        if (*fmt != '%')
        {
            tmp[0] = *fmt;
            tmp[1] = '\0';
        }
        else
        {
            switch (*++fmt)
            {
                case 'Y': snprintf(tmp, sizeof(tmp), "%d", tm->tm_year + 1900); break;
                case 'm': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mon + 1);   break;
                case 'd': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_mday);      break;
                case 'H': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_hour);      break;
                case 'M': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_min);       break;
                case 'S': snprintf(tmp, sizeof(tmp), "%02d", tm->tm_sec);       break;
                case 'j': snprintf(tmp, sizeof(tmp), "%03d", tm->tm_yday + 1);  break;
                case 'a': piece = day_names[tm->tm_wday % 7];                   break;
                case 'b': piece = month_names[tm->tm_mon % 12];                 break;
                case '%': piece = "%";                                          break;
                case '\0': fmt--; piece = "";                                   break;
                default:  tmp[0] = '%'; tmp[1] = *fmt; tmp[2] = '\0';           break;
            }
        }

        for (; *piece; piece++)
        {
            if (n + 1 >= size)
                return 0;

            buf[n++] = *piece;
        }
    }

    if (size == 0)
        return 0;

    buf[n] = '\0';

    return n;
}
