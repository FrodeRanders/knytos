/* Explicit pinned time-record probe, not the complete native sys.h. */
#include "erts_time_init.h"
#include "clock-policy.h"
#ifndef KNYTOS_SERVICE_WORKER
#error "Native clock SDK requires the service-worker profile"
#endif
_Static_assert(ERTS_KNYTOS_CLOCK_MAX_AGE_MS == KNYTOS_ERTS_SDK_CLOCK_MAX_AGE_MS,
               "Native clock SDK sample-age policy mismatch");
_Static_assert(sizeof(ErtsMonotonicTime) == 8 && sizeof(ErtsSystemTime) == 8,
               "Native clock SDK requires signed LP64 nanoseconds");
static void (*volatile ref_sys_init_time)(ErtsSysInitTimeResult *) = sys_init_time;
_Static_assert(__builtin_types_compatible_p(__typeof__(sys_init_time), __typeof__(*ref_sys_init_time)), "Native clock signature mismatch: sys_init_time");
static void (*volatile ref_erts_late_sys_init_time)(void) = erts_late_sys_init_time;
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_late_sys_init_time), __typeof__(*ref_erts_late_sys_init_time)), "Native clock signature mismatch: erts_late_sys_init_time");
static int (*volatile ref_knytos_erts_init_time)(ErtsSysInitTimeResult *, uint64_t) = knytos_erts_init_time;
_Static_assert(__builtin_types_compatible_p(__typeof__(knytos_erts_init_time), __typeof__(*ref_knytos_erts_init_time)), "Native clock signature mismatch: knytos_erts_init_time");
static ErtsMonotonicTime (*volatile ref_erts_os_monotonic_time)(void) = erts_os_monotonic_time;
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_os_monotonic_time), __typeof__(*ref_erts_os_monotonic_time)), "Native clock signature mismatch: erts_os_monotonic_time");
static ErtsSysHrTime (*volatile ref_erts_sys_hrtime)(void) = erts_sys_hrtime;
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_sys_hrtime), __typeof__(*ref_erts_sys_hrtime)), "Native clock signature mismatch: erts_sys_hrtime");
static ErtsSystemTime (*volatile ref_erts_os_system_time)(void) = erts_os_system_time;
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_os_system_time), __typeof__(*ref_erts_os_system_time)), "Native clock signature mismatch: erts_os_system_time");
static void (*volatile ref_erts_os_times)(ErtsMonotonicTime *, ErtsSystemTime *) = erts_os_times;
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_os_times), __typeof__(*ref_erts_os_times)), "Native clock signature mismatch: erts_os_times");
static int (*volatile ref_knytos_erts_clock_publish)(const struct knytos_realtime_sample *, uint64_t) = knytos_erts_clock_publish;
_Static_assert(__builtin_types_compatible_p(__typeof__(knytos_erts_clock_publish), __typeof__(*ref_knytos_erts_clock_publish)), "Native clock signature mismatch: knytos_erts_clock_publish");
static int (*volatile ref_knytos_erts_clock_refresh)(uint64_t) = knytos_erts_clock_refresh;
_Static_assert(__builtin_types_compatible_p(__typeof__(knytos_erts_clock_refresh), __typeof__(*ref_knytos_erts_clock_refresh)), "Native clock signature mismatch: knytos_erts_clock_refresh");
static int (*volatile ref_knytos_erts_clock_read)(ErtsSystemTime *) = knytos_erts_clock_read;
_Static_assert(__builtin_types_compatible_p(__typeof__(knytos_erts_clock_read), __typeof__(*ref_knytos_erts_clock_read)), "Native clock signature mismatch: knytos_erts_clock_read");
static ErtsSysPerfCounter (*volatile ref_erts_sys_perf_counter)(void) = erts_sys_perf_counter;
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_sys_perf_counter), __typeof__(*ref_erts_sys_perf_counter)), "Native clock signature mismatch: erts_sys_perf_counter");
static ErtsSysPerfCounter (*volatile ref_erts_sys_perf_counter_unit)(void) = erts_sys_perf_counter_unit;
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_sys_perf_counter_unit), __typeof__(*ref_erts_sys_perf_counter_unit)), "Native clock signature mismatch: erts_sys_perf_counter_unit");
