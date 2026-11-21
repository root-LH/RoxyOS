#include "../include/rtc.h"
#include "../include/io.h"
#include "../include/cpu.h"
#include "../include/pit.h"
#include "../include/printk.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

/* Bit 7 of the address port disables NMIs while we talk to the CMOS. */
#define CMOS_NMI_OFF 0x80

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B
#define RTC_CENTURY  0x32   /* not standard, but QEMU and most PCs have it */

#define STATUS_A_UPDATING 0x80
#define STATUS_B_24HOUR   0x02
#define STATUS_B_BINARY   0x04
#define HOUR_PM           0x80

static uint32_t boot_epoch = 0;
static uint32_t boot_tick = 0;

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDRESS, CMOS_NMI_OFF | reg);
    return inb(CMOS_DATA);
}

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

typedef struct
{
    uint8_t second, minute, hour, day, month, year, century;
} raw_time_t;

static void read_raw(raw_time_t *r)
{
    /* The registers change during the ~2 ms update cycle: wait it out. */
    for (int i = 0; i < 100000 && (cmos_read(RTC_STATUS_A) & STATUS_A_UPDATING); i++)
        ;

    r->second = cmos_read(RTC_SECONDS);
    r->minute = cmos_read(RTC_MINUTES);
    r->hour = cmos_read(RTC_HOURS);
    r->day = cmos_read(RTC_DAY);
    r->month = cmos_read(RTC_MONTH);
    r->year = cmos_read(RTC_YEAR);
    r->century = cmos_read(RTC_CENTURY);
}

static int raw_equal(const raw_time_t *a, const raw_time_t *b)
{
    return a->second == b->second && a->minute == b->minute &&
           a->hour == b->hour && a->day == b->day &&
           a->month == b->month && a->year == b->year &&
           a->century == b->century;
}

static int is_leap(uint32_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint32_t days_in_month(uint32_t year, uint32_t month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    return month == 2 && is_leap(year) ? 29 : days[month - 1];
}

void rtc_read(rtc_time_t *out)
{
    raw_time_t a, b;
    uint32_t flags = irq_save();

    /* Read until two reads agree, so an update between registers cannot tear it. */
    read_raw(&a);

    for (int tries = 0; tries < 8; tries++)
    {
        read_raw(&b);

        if (raw_equal(&a, &b))
            break;

        a = b;
    }

    uint8_t status_b = cmos_read(RTC_STATUS_B);

    irq_restore(flags);

    int pm = a.hour & HOUR_PM;
    a.hour &= (uint8_t)~HOUR_PM;

    if (!(status_b & STATUS_B_BINARY))
    {
        a.second = from_bcd(a.second);
        a.minute = from_bcd(a.minute);
        a.hour = from_bcd(a.hour);
        a.day = from_bcd(a.day);
        a.month = from_bcd(a.month);
        a.year = from_bcd(a.year);
        a.century = from_bcd(a.century);
    }

    /* 12-hour mode: 12 AM is 0 h, 12 PM stays 12 h. */
    if (!(status_b & STATUS_B_24HOUR))
    {
        a.hour %= 12;

        if (pm)
            a.hour += 12;
    }

    uint32_t century = (a.century >= 19 && a.century <= 99) ? a.century : 20;

    out->year = century * 100 + a.year;
    out->month = a.month;
    out->day = a.day;
    out->hour = a.hour;
    out->minute = a.minute;
    out->second = a.second;

    /* Re-derive the weekday from the date (the CMOS register is unreliable). */
    rtc_time_t tmp;
    rtc_from_epoch(rtc_to_epoch(out), &tmp);
    out->weekday = tmp.weekday;
}

uint32_t rtc_to_epoch(const rtc_time_t *t)
{
    if (t->year < 1970 || t->month < 1 || t->month > 12 || t->day < 1)
        return 0;

    uint32_t days = 0;

    for (uint32_t y = 1970; y < t->year; y++)
        days += is_leap(y) ? 366 : 365;

    for (uint32_t m = 1; m < t->month; m++)
        days += days_in_month(t->year, m);

    days += t->day - 1u;

    return days * 86400u + t->hour * 3600u + t->minute * 60u + t->second;
}

void rtc_from_epoch(uint32_t epoch, rtc_time_t *out)
{
    uint32_t days = epoch / 86400;
    uint32_t rest = epoch % 86400;

    out->hour = (uint8_t)(rest / 3600);
    out->minute = (uint8_t)(rest / 60 % 60);
    out->second = (uint8_t)(rest % 60);

    /* 1970-01-01 was a Thursday. */
    out->weekday = (uint8_t)((days + 4) % 7);

    uint32_t year = 1970;

    while (days >= (is_leap(year) ? 366u : 365u))
    {
        days -= is_leap(year) ? 366 : 365;
        year++;
    }

    uint32_t month = 1;

    while (days >= days_in_month(year, month))
    {
        days -= days_in_month(year, month);
        month++;
    }

    out->year = year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)(days + 1);
}

void rtc_init(void)
{
    rtc_time_t now;

    rtc_read(&now);

    boot_epoch = rtc_to_epoch(&now);
    boot_tick = pit_get_ticks();

    klog("[rtc] %u-%02u-%02u %02u:%02u:%02u\n",
         now.year, now.month, now.day, now.hour, now.minute, now.second);
}

uint32_t rtc_now(void)
{
    return boot_epoch + (pit_get_ticks() - boot_tick) / PIT_HZ;
}

uint32_t rtc_boot_time(void)
{
    return boot_epoch;
}
