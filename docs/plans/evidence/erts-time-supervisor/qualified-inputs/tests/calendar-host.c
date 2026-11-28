/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "erts_calendar.h"

/* The test build renames the production gmtime_r call for one controlled
 * conversion failure. Other cases delegate to the actual host libc. */
#undef gmtime_r
extern struct tm *gmtime_r(const time_t *, struct tm *);
static int clock_status, reads, conversion_failed, diagnostic;
static ErtsSystemTime current;

int knytos_erts_clock_read(ErtsSystemTime *output)
{
    ++reads;
    if (!clock_status) *output = current;
    return clock_status;
}

struct tm *knytos_test_gmtime_r(const time_t *seconds, struct tm *output)
{
    return conversion_failed ? NULL : gmtime_r(seconds, output);
}

int32_t knytos_console_write(const void *message, size_t size)
{
    static const char expected[] = "[erts] UTC calendar unavailable stale or out of range\n";
    diagnostic = size == sizeof(expected) - 1 && !memcmp(message, expected, size);
    return KNYTOS_UNAVAILABLE;
}

_Noreturn void abort(void)
{
    _Exit(diagnostic ? 42 : 43);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int mode = atoi(argv[1]);
    struct tm output, original;
    memset(&output, 0xa5, sizeof(output));
    original = output;
    current = 951827696123456789L; /* 2000-02-29 12:34:56 UTC */
    if (mode == 0) {
        assert(knytos_erts_calendar_now(&output) == KNYTOS_OK);
        assert(output.tm_year == 100 && output.tm_mon == 1 && output.tm_mday == 29);
        assert(output.tm_hour == 12 && output.tm_min == 34 && output.tm_sec == 56);
        assert(output.tm_wday == 2 && output.tm_yday == 59 && output.tm_isdst == 0);
        assert(setenv("TZ", "EST5EDT", 1) == 0);
        tzset();
        assert(knytos_erts_calendar_now(&output) == KNYTOS_OK && output.tm_hour == 12 && !output.tm_isdst);
    } else if (mode == 1) {
        current = LONG_MAX;
        assert(knytos_erts_calendar_now(&output) == KNYTOS_OK);
        assert(output.tm_year == 362 && output.tm_mon == 3 && output.tm_mday == 11);
        assert(output.tm_hour == 23 && output.tm_min == 47 && output.tm_sec == 16);
        current = 999999999;
        assert(knytos_erts_calendar_now(&output) == KNYTOS_OK && output.tm_sec == 0);
    } else if (mode == 2) {
        assert(knytos_erts_calendar_now(NULL) == KNYTOS_INVALID && reads == 0);
        _Alignas(struct tm) char storage[sizeof(struct tm) + _Alignof(struct tm)];
        assert(knytos_erts_calendar_now((struct tm *)(storage + 1)) == KNYTOS_INVALID && reads == 0);
    } else if (mode == 3) {
        clock_status = KNYTOS_UNAVAILABLE;
        assert(knytos_erts_calendar_now(&output) == KNYTOS_UNAVAILABLE);
        assert(!memcmp(&output, &original, sizeof(output)));
    } else if (mode == 4) {
        current = -1;
        assert(knytos_erts_calendar_now(&output) == KNYTOS_INVALID);
        assert(!memcmp(&output, &original, sizeof(output)));
    } else if (mode == 5) {
        conversion_failed = 1;
        assert(knytos_erts_calendar_now(&output) == KNYTOS_OVERFLOW);
        assert(!memcmp(&output, &original, sizeof(output)));
    } else if (mode == 6) {
        clock_status = KNYTOS_UNAVAILABLE;
        knytos_erts_calendar_now_or_abort(&output);
        return 44;
    } else {
        assert(mode == 7);
        knytos_erts_calendar_now_or_abort(&output);
        assert(output.tm_year == 100 && !output.tm_isdst);
    }
    return 0;
}
