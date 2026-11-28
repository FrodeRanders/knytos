dnl SPDX-License-Identifier: Apache-2.0
dnl Explicit bounded clock probe; does not implement the complete native sys layer.
AC_DEFUN([ERL_KNYTOS_CLOCK], [
  AS_CASE([$host], [aarch64-unknown-knytos], [],
          [AC_MSG_ERROR([the Knytos clock SDK requires aarch64-unknown-knytos])])
  AS_IF([test "x$ETHR_THR_LIB_BASE" != xknytos],
        [AC_MSG_ERROR([the Knytos clock SDK requires native Knytos ethread])])
  AS_CASE([$with_knytos_clock_max_age_ms], [""|*[[!0-9]]*],
          [AC_MSG_ERROR([a bounded nonzero Knytos clock sample age is required])])
  AS_CASE([$with_knytos_clock_max_age_ms], [0*],
          [AC_MSG_ERROR([Knytos clock sample age must be 1..2147483647 milliseconds without leading zeroes])])
  AS_IF([test "${#with_knytos_clock_max_age_ms}" -gt 10 ||
         test "$with_knytos_clock_max_age_ms" -gt 2147483647],
        [AC_MSG_ERROR([Knytos clock sample age must be 1..2147483647 milliseconds])])
  AS_IF([test -n "$with_clock_resolution$with_clock_gettime_realtime_id$with_clock_gettime_monotonic_id" ||
         test "x$enable_gettimeofday_as_os_system_time" = xyes ||
         test "x$enable_prefer_elapsed_monotonic_time_during_suspend" = xyes],
        [AC_MSG_ERROR([native Knytos clocks cannot select POSIX clock options])])

  knytos_clock_saved_CPPFLAGS=$CPPFLAGS
  knytos_clock_saved_LIBS=$LIBS
  CPPFLAGS="$CPPFLAGS -I$with_knytos_clock/clock -DKNYTOS_ERTS_TIME_CONTRACT_PROBE -DERTS_KNYTOS_CLOCK_MAX_AGE_MS=$with_knytos_clock_max_age_ms"
  LIBS="$with_knytos_clock/libknytos_erts_clock.a $LIBS"
  AC_MSG_CHECKING([for the bounded native Knytos clock interface])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <clock-link-check.h>
  ]], [[
    return ref_sys_init_time == 0 || ref_erts_late_sys_init_time == 0
      || ref_knytos_erts_init_time == 0 || ref_erts_os_monotonic_time == 0
      || ref_erts_sys_hrtime == 0 || ref_erts_os_system_time == 0
      || ref_erts_os_times == 0 || ref_knytos_erts_clock_publish == 0
      || ref_knytos_erts_clock_refresh == 0 || ref_knytos_erts_clock_read == 0
      || ref_erts_sys_perf_counter == 0 || ref_erts_sys_perf_counter_unit == 0;
  ]])], [
    knytos_clock_link=yes
    cp conftest.$ac_ext knytos-clock-link.c
    cp conftest$ac_exeext knytos-clock-link.elf
  ], [knytos_clock_link=no])
  AC_MSG_RESULT([$knytos_clock_link])
  CPPFLAGS=$knytos_clock_saved_CPPFLAGS
  LIBS=$knytos_clock_saved_LIBS
  AS_IF([test "$knytos_clock_link" != yes],
        [AC_MSG_ERROR([native Knytos clock interface did not compile and link])])

  AC_DEFINE([ERTS_KNYTOS_CLOCK], [1], [Select the native Knytos clock interface])
  AC_DEFINE_UNQUOTED([ERTS_KNYTOS_CLOCK_MAX_AGE_NS],
                    [($with_knytos_clock_max_age_ms*1000000ULL)],
                    [Explicit bounded native clock sample-age policy])
  AC_DEFINE([HAVE_MONOTONIC_ERTS_SYS_HRTIME], [1], [Native high-resolution monotonic time])
  dnl No POSIX clock IDs, corrected-clock or CPU-time claims. The native
  dnl platform header supplies the nanosecond units and local perf counter.
  LIBRT=
  AC_SUBST([LIBRT])
  AC_MSG_WARN([native clock probe only: sys header/source routing and refresh ownership remain unqualified])
])
