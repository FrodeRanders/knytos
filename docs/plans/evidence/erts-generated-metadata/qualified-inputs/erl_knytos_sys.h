/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ERL_KNYTOS_SYS_H
#define ERL_KNYTOS_SYS_H

/* Minimal native platform surface for the complete common sys.h and clock
 * objects. Unimplemented platform operations retain their upstream declarations;
 * this header supplies no Unix descriptor, signal or CPU-accounting backend. */
#if !defined(ERTS_KNYTOS) || !defined(ERTS_KNYTOS_CLOCK) || !defined(ETHR_KNYTOS)
#error "The native Knytos sys header requires native platform, clock and ethread selection"
#endif
#if defined(UNIX) || defined(__WIN32__) || defined(ETHR_PTHREADS) || defined(ETHR_WIN32_THREADS)
#error "The native Knytos sys header cannot select another platform"
#endif
#ifdef KNYTOS_ERTS_TIME_CONTRACT_PROBE
#error "The native Knytos sys header requires the complete common time record"
#endif
#if !defined(KNYTOS_NATIVE_WORKERS) || !defined(KNYTOS_WORKER_LIBC) || !defined(KNYTOS_SERVICE_WORKER)
#error "The native Knytos sys header requires the service-worker C profile"
#endif
#ifdef WANT_NONBLOCKING
#error "Native Knytos descriptor readiness is not implemented"
#endif

#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "erl_errno.h"
#include "erts_clock_platform.h"

/* The initial headless profile permits upstream's +A0 path. Common
 * erts_init_async creates no async pool at zero; positive pool sizes still
 * require independent native thread-capacity and driver qualification. */
#define ERTS_MIN_NO_OF_ASYNC_THREADS 0

/* Only the common timer's remaining-duration record. This grants no POSIX
 * time API and does not provide erts_time_remaining or timer delivery. */
typedef struct {
    long tv_sec;
    long tv_usec;
} SysTimeval;

#endif
