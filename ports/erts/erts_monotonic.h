/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ERTS_MONOTONIC_H
#define KNYTOS_ERTS_MONOTONIC_H

/* Standalone declarations for the pinned LP64 target. A future native sys
 * header must select these explicitly; this is not erl_unix_sys.h. */
#if __SIZEOF_LONG__ != 8
#error "Knytos ERTS clocks require the qualified LP64 target"
#endif
typedef long ErtsMonotonicTime;
typedef long ErtsSysHrTime;

#define ERTS_COMPILE_TIME_MONOTONIC_TIME_UNIT (1000*1000*1000)
#define ERTS_HRTIME_UNIT (1000*1000*1000)

/* Local scalar reads, without allocation, RPC, waiting or mutable clock
 * state. Failure and exhaustion of the signed nanosecond range terminate
 * the domain: these ERTS interfaces have no error result. They rely on the
 * kernel's monotonic counter contract and do not imply frequency correction,
 * security time, UTC availability or cross-machine comparability. */
ErtsMonotonicTime erts_os_monotonic_time(void);
ErtsSysHrTime erts_sys_hrtime(void);

#endif
