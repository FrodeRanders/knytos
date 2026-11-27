dnl SPDX-License-Identifier: Apache-2.0
dnl Explicit native backend: no pthread or POSIX feature answers are supplied.
AC_DEFUN([ERL_FIND_KNYTOS_ETHR_LIB], [
  AS_CASE([$host_os], [knytos*], [],
          [AC_MSG_ERROR([the Knytos ethread SDK requires a Knytos target])])
  AS_IF([test "x$enable_native_ethr_impls" = xno],
        [AC_MSG_ERROR([the Knytos backend requires native atomic implementations])])

  AC_CHECK_SIZEOF([void *])
  AC_CHECK_SIZEOF([int])
  AC_CHECK_SIZEOF([long])
  AC_CHECK_SIZEOF([long long])
  AC_CHECK_SIZEOF([__int64])
  AC_CHECK_SIZEOF([__int128_t])
  AC_DEFINE_UNQUOTED([ETHR_SIZEOF_PTR], [$ac_cv_sizeof_void_p], [Size of pointers])
  AC_DEFINE_UNQUOTED([ETHR_SIZEOF_INT], [$ac_cv_sizeof_int], [Size of int])
  AC_DEFINE_UNQUOTED([ETHR_SIZEOF_LONG], [$ac_cv_sizeof_long], [Size of long])
  AC_DEFINE_UNQUOTED([ETHR_SIZEOF_LONG_LONG], [$ac_cv_sizeof_long_long], [Size of long long])
  AC_DEFINE_UNQUOTED([ETHR_SIZEOF___INT64], [$ac_cv_sizeof___int64], [Size of __int64])
  AC_DEFINE_UNQUOTED([ETHR_SIZEOF___INT128_T], [$ac_cv_sizeof___int128_t], [Size of __int128_t])
  AC_DEFINE([ETHR_KNYTOS], [1], [Select the native Knytos ethread backend])
  AC_DEFINE([ETHR_HAVE_ETHREAD_DEFINES], [1], [Native ethread configuration is available])

  knytos_saved_CPPFLAGS=$CPPFLAGS
  knytos_saved_LIBS=$LIBS
  CPPFLAGS="$CPPFLAGS -I$with_knytos_ethread/include/internal -I$with_knytos_ethread"
  LIBS="$with_knytos_ethread/libknytos_ethread.a $LIBS"
  AC_MSG_CHECKING([for the qualified native Knytos ethread interface])
  dnl The SDK header retains every checked public/native API reference. A
  dnl successful link must resolve the archive, even under --gc-sections.
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <ethread-link-check.h>
  ]], [[return 0;]])], [
    knytos_ethread_link=yes
    cp conftest.$ac_ext knytos-ethread-link.c
    cp conftest$ac_exeext knytos-ethread-link.elf
  ], [knytos_ethread_link=no])
  AC_MSG_RESULT([$knytos_ethread_link])
  CPPFLAGS=$knytos_saved_CPPFLAGS
  LIBS=$knytos_saved_LIBS
  AS_IF([test "$knytos_ethread_link" != yes],
        [AC_MSG_ERROR([native Knytos ethread interface did not compile and link])])

  ETHR_THR_LIB_BASE=knytos
  ETHR_THR_LIB_BASE_TYPE=knytos
  ETHR_THR_LIB_BASE_DIR=knytos
  ETHR_LIB_NAME=ethread
  ETHR_X_LIBS=
  ETHR_LIBS="-lethread"
  ETHR_DEFS="-DUSE_THREADS -DETHR_KNYTOS -DKNYTOS_NATIVE_WORKERS -DKNYTOS_WORKER_LIBC -D__STDIO_LOCKING"
  ETHR_X86_SSE2_ASM=no
  dnl These are properties enforced by the selected C headers and the real
  dnl compile/link check, not cached answers about another platform.
  ethr_have_native_atomics=yes
  ethr_native_atomic_implementation=knytos_c11
  ethr_have_native_spinlock=no
  AC_SUBST([ETHR_THR_LIB_BASE])
  AC_SUBST([ETHR_THR_LIB_BASE_DIR])
  AC_SUBST([ETHR_LIB_NAME])
  AC_SUBST([ETHR_X_LIBS])
  AC_SUBST([ETHR_LIBS])
  AC_SUBST([ETHR_DEFS])
  AC_SUBST([ETHR_X86_SSE2_ASM])
])
