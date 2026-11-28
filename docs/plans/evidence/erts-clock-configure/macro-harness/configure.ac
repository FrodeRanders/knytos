AC_INIT([knytos-clock-contract], [1])
AC_CONFIG_SRCDIR([knytos-clock.m4])
m4_include([knytos-clock.m4])
AC_CANONICAL_HOST
AC_PROG_CC
AC_ARG_VAR([ETHR_THR_LIB_BASE], [selected thread backend])
AC_ARG_WITH([knytos-clock])
AC_ARG_WITH([knytos-clock-max-age-ms])
AC_ARG_WITH([clock-resolution])
AC_ARG_WITH([clock-gettime-realtime-id])
AC_ARG_WITH([clock-gettime-monotonic-id])
AC_ARG_ENABLE([gettimeofday-as-os-system-time])
AC_ARG_ENABLE([prefer-elapsed-monotonic-time-during-suspend])
ERL_KNYTOS_CLOCK
AC_CONFIG_HEADERS([selected-clock.h])
AC_CONFIG_FILES([selected-libs])
AC_OUTPUT
