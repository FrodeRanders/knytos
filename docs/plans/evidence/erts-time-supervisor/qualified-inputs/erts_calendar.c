/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <stdlib.h>
#include "erts_calendar.h"

_Static_assert(sizeof(time_t) == 8 && (time_t)-1 < 0,
               "Native calendar requires signed 64-bit time_t");

int knytos_erts_calendar_now(struct tm *output)
{
    if (!output || (uintptr_t)output % _Alignof(struct tm)) return KNYTOS_INVALID;
    ErtsSystemTime nanoseconds;
    int status = knytos_erts_clock_read(&nanoseconds);
    if (status != KNYTOS_OK) return status;
    if (nanoseconds < 0) return KNYTOS_INVALID;
    time_t seconds = nanoseconds / ERTS_COMPILE_TIME_MONOTONIC_TIME_UNIT;
    struct tm prepared = {0};
    if (!gmtime_r(&seconds, &prepared)) return KNYTOS_OVERFLOW;
    *output = prepared;
    return KNYTOS_OK;
}

void knytos_erts_calendar_now_or_abort(struct tm *output)
{
    if (knytos_erts_calendar_now(output) != KNYTOS_OK) {
        static const char message[] = "[erts] UTC calendar unavailable stale or out of range\n";
        (void)knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
}
