#include "config.h"
#include "sys.h"
#include "erts_time_init.h"
#include "clock-link-check.h"
#if defined(UNIX) || defined(_ERL_UNIX_SYS_H) || defined(KNYTOS_ERTS_TIME_CONTRACT_PROBE)
#error "Native consumer selected a substitute sys or time record"
#endif
typedef struct {
    int have_os_monotonic_time;
    int have_corrected_os_monotonic_time;
    ErtsMonotonicTime os_monotonic_time_unit;
    ErtsMonotonicTime sys_clock_resolution;
    struct {
	Uint64 resolution;
        Uint64 used_resolution;
	char *func;
	char *clock_id;
	int locked_use;
	int extended;
    } os_monotonic_time_info;
    struct {
	Uint64 resolution;
	char *func;
	char *clock_id;
	int locked_use;
    } os_system_time_info;
} ExpectedTimeResult;
_Static_assert(sizeof(ErtsSysInitTimeResult) == sizeof(ExpectedTimeResult), "Native time record layout mismatch");
_Static_assert(_Alignof(ErtsSysInitTimeResult) == _Alignof(ExpectedTimeResult), "Native time record layout mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, have_os_monotonic_time) == offsetof(ExpectedTimeResult, have_os_monotonic_time), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->have_os_monotonic_time), __typeof__(((ExpectedTimeResult *)0)->have_os_monotonic_time)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, have_corrected_os_monotonic_time) == offsetof(ExpectedTimeResult, have_corrected_os_monotonic_time), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->have_corrected_os_monotonic_time), __typeof__(((ExpectedTimeResult *)0)->have_corrected_os_monotonic_time)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_unit) == offsetof(ExpectedTimeResult, os_monotonic_time_unit), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_unit), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_unit)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, sys_clock_resolution) == offsetof(ExpectedTimeResult, sys_clock_resolution), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->sys_clock_resolution), __typeof__(((ExpectedTimeResult *)0)->sys_clock_resolution)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.resolution) == offsetof(ExpectedTimeResult, os_monotonic_time_info.resolution), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.resolution), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.resolution)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.used_resolution) == offsetof(ExpectedTimeResult, os_monotonic_time_info.used_resolution), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.used_resolution), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.used_resolution)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.func) == offsetof(ExpectedTimeResult, os_monotonic_time_info.func), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.func), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.func)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.clock_id) == offsetof(ExpectedTimeResult, os_monotonic_time_info.clock_id), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.clock_id), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.clock_id)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.locked_use) == offsetof(ExpectedTimeResult, os_monotonic_time_info.locked_use), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.locked_use), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.locked_use)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.extended) == offsetof(ExpectedTimeResult, os_monotonic_time_info.extended), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.extended), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.extended)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.resolution) == offsetof(ExpectedTimeResult, os_system_time_info.resolution), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.resolution), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.resolution)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.func) == offsetof(ExpectedTimeResult, os_system_time_info.func), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.func), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.func)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.clock_id) == offsetof(ExpectedTimeResult, os_system_time_info.clock_id), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.clock_id), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.clock_id)), "Native time record field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.locked_use) == offsetof(ExpectedTimeResult, os_system_time_info.locked_use), "Native time record offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.locked_use), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.locked_use)), "Native time record field mismatch");
int main(void) { return ref_sys_init_time == 0 || ref_erts_late_sys_init_time == 0 || ref_knytos_erts_init_time == 0 || ref_erts_os_monotonic_time == 0 || ref_erts_sys_hrtime == 0 || ref_erts_os_system_time == 0 || ref_erts_os_times == 0 || ref_knytos_erts_clock_publish == 0 || ref_knytos_erts_clock_refresh == 0 || ref_knytos_erts_clock_read == 0 || ref_erts_sys_perf_counter == 0 || ref_erts_sys_perf_counter_unit == 0; }
