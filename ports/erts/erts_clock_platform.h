/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ERTS_CLOCK_PLATFORM_H
#define KNYTOS_ERTS_CLOCK_PLATFORM_H

#include "erts_system_time.h"

/* Standalone native clock section, not a complete ERTS sys header. */
typedef ErtsSysHrTime ErtsSysPerfCounter;
#define ERTS_HAVE_OS_MONOTONIC_TIME_SUPPORT 1
#define ERTS_MONOTONIC_TIME_MIN ((ErtsMonotonicTime)(1ULL << 63))
#define ERTS_MONOTONIC_TIME_MAX (~ERTS_MONOTONIC_TIME_MIN)
/* The generic ERTS timer-wheel unit is milliseconds; this is not a claim
 * about oscillator granularity, CPU accounting or timer delivery latency. */
#define SYS_CLOCK_RESOLUTION 1

static inline ErtsSysPerfCounter erts_sys_perf_counter(void)
{
    return erts_sys_hrtime();
}

static inline ErtsSysPerfCounter erts_sys_perf_counter_unit(void)
{
    return ERTS_HRTIME_UNIT;
}

#endif
