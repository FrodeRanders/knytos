#include "erts_system_time.h"
typedef long OtpErtsMonotonicTime;
typedef long OtpErtsSysHrTime;
typedef OtpErtsMonotonicTime OtpErtsSystemTime;
_Static_assert(__builtin_types_compatible_p(ErtsMonotonicTime, OtpErtsMonotonicTime), "OTP type mismatch: ErtsMonotonicTime");
_Static_assert(__builtin_types_compatible_p(ErtsSysHrTime, OtpErtsSysHrTime), "OTP type mismatch: ErtsSysHrTime");
_Static_assert(__builtin_types_compatible_p(ErtsSystemTime, OtpErtsSystemTime), "OTP type mismatch: ErtsSystemTime");
OtpErtsMonotonicTime otp_erts_os_monotonic_time(void);
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_os_monotonic_time), __typeof__(otp_erts_os_monotonic_time)), "OTP signature mismatch: erts_os_monotonic_time");
OtpErtsSysHrTime otp_erts_sys_hrtime(void);
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_sys_hrtime), __typeof__(otp_erts_sys_hrtime)), "OTP signature mismatch: erts_sys_hrtime");
unsigned long otp_erts_os_system_time(void);
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_os_system_time), __typeof__(otp_erts_os_system_time)), "OTP signature mismatch: erts_os_system_time");
void otp_erts_os_times(OtpErtsMonotonicTime *, OtpErtsSystemTime *);
_Static_assert(__builtin_types_compatible_p(__typeof__(erts_os_times), __typeof__(otp_erts_os_times)), "OTP signature mismatch: erts_os_times");

static ErtsMonotonicTime (*volatile mono)(void) = erts_os_monotonic_time;
static ErtsSysHrTime (*volatile high)(void) = erts_sys_hrtime;
static ErtsSystemTime (*volatile system_time)(void) = erts_os_system_time;
static void (*volatile paired)(ErtsMonotonicTime *, ErtsSystemTime *) = erts_os_times;
int main(void) {
    ErtsMonotonicTime m;
    ErtsSystemTime s = system_time();
    struct knytos_realtime_sample sample = {0};
    paired(&m, &s);
    return knytos_erts_clock_refresh(5000000000ULL)
        + knytos_erts_clock_publish(&sample, 5000000000ULL)
        + knytos_erts_clock_read(&s) + (mono() > high());
}
