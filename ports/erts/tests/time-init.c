/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "erts_time_init.h"
#ifdef KNYTOS_ERTS_CALENDAR_CHECK
#include "calendar-values.h"
#endif

#ifndef CASE
#define CASE 92
#endif

static void check(const ErtsSysInitTimeResult *result)
{
    assert(result->have_os_monotonic_time == 1 && !result->have_corrected_os_monotonic_time);
    assert(result->os_monotonic_time_unit == 1000000000 && result->sys_clock_resolution == 1);
    assert(result->os_monotonic_time_info.resolution >= 1 && result->os_monotonic_time_info.resolution <= 1000000000);
    assert(result->os_monotonic_time_info.used_resolution == result->os_monotonic_time_info.resolution);
    assert(result->os_system_time_info.resolution >= 1 && result->os_system_time_info.resolution <= 1000000000);
    assert(!result->os_monotonic_time_info.clock_id && !result->os_system_time_info.clock_id);
    assert(!result->os_monotonic_time_info.locked_use && !result->os_monotonic_time_info.extended);
    assert(!result->os_system_time_info.locked_use);
    assert(!strcmp(result->os_monotonic_time_info.func, "knytos_clock_monotonic_ns"));
    assert(!strcmp(result->os_system_time_info.func, "knytos_clock_sample_project_ns"));
    ErtsMonotonicTime before = erts_os_monotonic_time();
    ErtsSysPerfCounter counter = erts_sys_perf_counter();
    assert(before <= counter && counter <= erts_os_monotonic_time());
    assert(erts_sys_perf_counter_unit() == 1000000000);
    assert(erts_os_system_time() > 1577836800000000000L);
}

static void worker(void *argument)
{
    check(argument);
#ifdef KNYTOS_ERTS_CALENDAR_CHECK
    calendar_current();
#endif
}

int main(void)
{
#ifdef KNYTOS_ERTS_PLATFORM_CHECK
    /* The generated Make profile retains OTP's stack-protector flags. Its
     * linked Picolibc constructor must run before protected clock functions.
     * Nonzero initialization does not claim a random or secret canary. */
    extern uintptr_t __stack_chk_guard;
    assert(__stack_chk_guard != 0);
#endif
#ifdef KNYTOS_ERTS_CALENDAR_CHECK
    if (CASE == 92) calendar_values();
    if (CASE == 95) {
        int y,m,d,h,min,s;
        get_localtime(&y,&m,&d,&h,&min,&s);
        assert(0 && "cold calendar must abort");
    }
#endif
    ErtsSysInitTimeResult result, original;
    memset(&result, 0xa5, sizeof(result));
    original = result;
    if (CASE == 93) {
        assert(knytos_erts_init_time(&result, 5000000000ULL) == KNYTOS_SERVICE_UNAUTHORIZED);
        assert(!memcmp(&result, &original, sizeof(result)));
        ErtsSystemTime unchanged = -123;
        assert(knytos_erts_clock_read(&unchanged) == KNYTOS_UNAVAILABLE && unchanged == -123);
#ifdef KNYTOS_ERTS_CALENDAR_CHECK
        struct tm calendar, original_calendar;
        memset(&calendar, 0xa5, sizeof(calendar));
        original_calendar = calendar;
        assert(knytos_erts_calendar_now(&calendar) == KNYTOS_UNAVAILABLE);
        assert(!memcmp(&calendar, &original_calendar, sizeof(calendar)));
#endif
        puts("ERTS TIME INIT missing grant preserves metadata and cold clock PASS");
        return 0;
    }
    sys_init_time(&result);
    assert(CASE != 94);
    check(&result);
    assert(knytos_thread_start(1, 1, worker, &result) == KNYTOS_OK);
#ifdef KNYTOS_ERTS_CALENDAR_CHECK
    calendar_current();
#endif
    assert(knytos_thread_join(1) == KNYTOS_OK);
    erts_late_sys_init_time();
    check(&result);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    puts("ERTS TIME INIT pinned metadata local performance counter and worker reads PASS");
    return 0;
}
