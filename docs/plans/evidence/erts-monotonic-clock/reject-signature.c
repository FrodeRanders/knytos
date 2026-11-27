#include "erts_monotonic.h"
typedef long OtpErtsMonotonicTime;
typedef long OtpErtsSysHrTime;
_Static_assert(__builtin_types_compatible_p(ErtsMonotonicTime, OtpErtsMonotonicTime), "OTP type mismatch: ErtsMonotonicTime");
_Static_assert(__builtin_types_compatible_p(ErtsSysHrTime, OtpErtsSysHrTime), "OTP type mismatch: ErtsSysHrTime");
unsigned long otp_erts_os_monotonic_time(void);
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_os_monotonic_time), __typeof__(otp_erts_os_monotonic_time)), "OTP signature mismatch: erts_os_monotonic_time");
OtpErtsSysHrTime otp_erts_sys_hrtime(void);
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_sys_hrtime), __typeof__(otp_erts_sys_hrtime)), "OTP signature mismatch: erts_sys_hrtime");
_Static_assert(ERTS_COMPILE_TIME_MONOTONIC_TIME_UNIT == 1000000000, "OTP unit mismatch: ERTS_COMPILE_TIME_MONOTONIC_TIME_UNIT");
_Static_assert(ERTS_HRTIME_UNIT == 1000000000, "OTP unit mismatch: ERTS_HRTIME_UNIT");
static ErtsMonotonicTime (*volatile mono)(void) = erts_os_monotonic_time;
static ErtsSysHrTime (*volatile high)(void) = erts_sys_hrtime;
int main(void) { return mono() > high(); }
