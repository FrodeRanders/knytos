/* confdefs.h */
#define PACKAGE_NAME ""
#define PACKAGE_TARNAME ""
#define PACKAGE_VERSION ""
#define PACKAGE_STRING ""
#define PACKAGE_BUGREPORT ""
#define PACKAGE_URL ""
#define ASSUMED_CACHE_LINE_SIZE 64
#define ERTS_ENABLE_MSACC 1
#define ERTS_NO_RETPOLINE 
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TIME_H 1
#define STDC_HEADERS 1
#define SIZEOF_TIME_T 8
#define SIZEOF_VOID_P 8
#define SIZEOF_VOID_P 8
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF___INT64 0
#define SIZEOF___INT128_T 16
#define ETHR_SIZEOF_PTR 8
#define ETHR_SIZEOF_INT 4
#define ETHR_SIZEOF_LONG 8
#define ETHR_SIZEOF_LONG_LONG 8
#define ETHR_SIZEOF___INT64 0
#define ETHR_SIZEOF___INT128_T 16
#define ETHR_KNYTOS 1
#define ETHR_HAVE_ETHREAD_DEFINES 1
#define ETHR_SMP_REQUIRE_NATIVE_IMPLS 1
#define ERTS_USE_BUILTIN_ZSTD 1
#define ERTS_USE_BUILTIN_ZLIB 1
#define HAVE_ZLIB_INFLATEGETDICTIONARY 1
#define ESOCK_ENABLE 1
#define HAVE_LOCALTIME_R 1
#define HAVE_STRFTIME 1
#define ESOCK_USE_EXTENDED_ERROR_INFO 1
#define ESOCK_VERBOSE_MTX_NAMES 1
#define ESOCK_COUNTER_SIZE 64
#define ESOCK_USE_SOCKET_REGISTRY 1
#define HAVE_DIRENT_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_IEEEFP_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_MALLOC_H 1
#define HAVE_LANGINFO_H 1
#define HAVE_POLL_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_DECL_GETRLIMIT 1
#define HAVE_DECL_SETRLIMIT 1
#define HAVE_DECL_RLIMIT_STACK 1
#define HAVE_SCHED_H 1
#define HAVE_NO_INADDR_LOOPBACK /**/
#define NO_SA_LEN 1
#define SIZEOF_CHAR 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 8
#define SIZEOF_VOID_P 8
#define SIZEOF_LONG_LONG 8
#define SIZEOF_SIZE_T 8
#define SIZEOF_OFF_T 8
#define SIZEOF_TIME_T 8
#define SIZEOF_SUSECONDS_T 4
#define SIZEOF__FLOAT16 2
#define FLOAT16_IS_CONVERTIBLE 1
#define ERTS_RESTRICT restrict
#define ERTS_ENDIANNESS -1
#define HAVE_FINITE 1
#define HAVE_ISNAN 1
#define HAVE_ISINF 1
#define HAVE_MEMMOVE 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_STRNCASECMP 1
#define HAVE_LOCALTIME_R 1
#define HAVE_GMTIME_R 1
#define HAVE_MEMCPY 1
#define HAVE_MEMRCHR 1
#define HAVE_FLOCKFILE 1
#define HAVE_STRLCPY 1
#define HAVE_STRLCAT 1
#define HAVE_SETLOCALE 1
#define HAVE_NL_LANGINFO 1
#define HAVE_ISFINITE 1
#define HAVE_DECL_POSIX2TIME 0
#define HAVE_DECL_TIME2POSIX 0
#define HAVE_DECL_DAYLIGHT 1
#define HAVE_VPRINTF 1
#define HAVE_CONFLICTING_FREAD_DECLARATION 1
#define HAVE_PUTC_UNLOCKED 1
#define HAVE_DECL_IN6ADDR_ANY_INIT 0
#define HAVE_DECL_IN6ADDR_LOOPBACK_INIT 0
#define HAVE_DECL_IPV6_V6ONLY 0
/* end confdefs.h.  */

#include <clock-link-check.h>

int
main (void)
{

    return ref_sys_init_time == 0 || ref_erts_late_sys_init_time == 0
      || ref_knytos_erts_init_time == 0 || ref_erts_os_monotonic_time == 0
      || ref_erts_sys_hrtime == 0 || ref_erts_os_system_time == 0
      || ref_erts_os_times == 0 || ref_knytos_erts_clock_publish == 0
      || ref_knytos_erts_clock_refresh == 0 || ref_knytos_erts_clock_read == 0
      || ref_erts_sys_perf_counter == 0 || ref_erts_sys_perf_counter_unit == 0;

  ;
  return 0;
}
