#include <stddef.h>
#include "erts_time_init.h"
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
_Static_assert(sizeof(ErtsSysInitTimeResult) == sizeof(ExpectedTimeResult), "OTP time layout mismatch");
_Static_assert(_Alignof(ErtsSysInitTimeResult) == _Alignof(ExpectedTimeResult), "OTP time layout mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, have_os_monotonic_time) == offsetof(ExpectedTimeResult, have_os_monotonic_time), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->have_os_monotonic_time), __typeof__(((ExpectedTimeResult *)0)->have_os_monotonic_time)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, have_corrected_os_monotonic_time) == offsetof(ExpectedTimeResult, have_corrected_os_monotonic_time), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->have_corrected_os_monotonic_time), __typeof__(((ExpectedTimeResult *)0)->have_corrected_os_monotonic_time)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_unit) == offsetof(ExpectedTimeResult, os_monotonic_time_unit), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_unit), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_unit)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, sys_clock_resolution) == offsetof(ExpectedTimeResult, sys_clock_resolution), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->sys_clock_resolution), __typeof__(((ExpectedTimeResult *)0)->sys_clock_resolution)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.resolution) == offsetof(ExpectedTimeResult, os_monotonic_time_info.resolution), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.resolution), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.resolution)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.used_resolution) == offsetof(ExpectedTimeResult, os_monotonic_time_info.used_resolution), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.used_resolution), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.used_resolution)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.func) == offsetof(ExpectedTimeResult, os_monotonic_time_info.func), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.func), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.func)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.clock_id) == offsetof(ExpectedTimeResult, os_monotonic_time_info.clock_id), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.clock_id), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.clock_id)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.locked_use) == offsetof(ExpectedTimeResult, os_monotonic_time_info.locked_use), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.locked_use), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.locked_use)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_monotonic_time_info.extended) == offsetof(ExpectedTimeResult, os_monotonic_time_info.extended), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_monotonic_time_info.extended), __typeof__(((ExpectedTimeResult *)0)->os_monotonic_time_info.extended)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.resolution) == offsetof(ExpectedTimeResult, os_system_time_info.resolution), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.resolution), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.resolution)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.func) == offsetof(ExpectedTimeResult, os_system_time_info.func), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.func), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.func)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.clock_id) == offsetof(ExpectedTimeResult, os_system_time_info.clock_id), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.clock_id), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.clock_id)), "OTP time field mismatch");
_Static_assert(offsetof(ErtsSysInitTimeResult, os_system_time_info.locked_use) == offsetof(ExpectedTimeResult, os_system_time_info.locked_use), "OTP time offset mismatch");
_Static_assert(__builtin_types_compatible_p(__typeof__(((ErtsSysInitTimeResult *)0)->os_system_time_info.locked_use), __typeof__(((ExpectedTimeResult *)0)->os_system_time_info.locked_use)), "OTP time field mismatch");

_Static_assert(__builtin_types_compatible_p(__typeof__(&sys_init_time), void (*)(ErtsSysInitTimeResult *)), "init signature");
_Static_assert(__builtin_types_compatible_p(__typeof__(&erts_late_sys_init_time), void (*)(void)), "late signature");
static void (*volatile init_time)(ErtsSysInitTimeResult *) = sys_init_time;
static void (*volatile late_time)(void) = erts_late_sys_init_time;
int main(void) {
    ErtsSysInitTimeResult result;
    init_time(&result);
    late_time();
    return knytos_erts_init_time(&result, 5000000000ULL)
        + (erts_sys_perf_counter() < 0) + (erts_sys_perf_counter_unit() != 1000000000);
}
