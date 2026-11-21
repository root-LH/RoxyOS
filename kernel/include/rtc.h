#ifndef RTC_H
#define RTC_H

#include "types.h"

/* A calendar date and time as the CMOS clock keeps it (no time zone). */
typedef struct
{
    uint32_t year;      /* e.g. 2026 */
    uint8_t month;      /* 1~12 */
    uint8_t day;        /* 1~31 */
    uint8_t hour;       /* 0~23 */
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;    /* 0 = Sunday (computed, not read from CMOS) */
} rtc_time_t;

/*
 * Reads the CMOS clock once and remembers it with the current tick, so
 * rtc_now() can count forward from the PIT afterwards.
 */
void rtc_init(void);

/* Reads the CMOS clock now (waits out an update in progress). */
void rtc_read(rtc_time_t *out);

/* Seconds since 1970-01-01 00:00 of the clock's date/time. */
uint32_t rtc_to_epoch(const rtc_time_t *t);
void rtc_from_epoch(uint32_t epoch, rtc_time_t *out);

/* Current time in epoch seconds: boot time from the RTC + PIT uptime. */
uint32_t rtc_now(void);

/* When rtc_init() read the clock, in epoch seconds. */
uint32_t rtc_boot_time(void);

#endif
