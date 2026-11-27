/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "knytos_ethread.h"

#ifndef CASE
#define CASE 76
#endif

static void finalizer(void)
{
    static const char marker[] = "ERTS DIAGNOSTICS unexpected finalizer\n";
    knytos_console_write(marker, sizeof(marker) - 1);
}

#if CASE != 76
static void *worker(void *argument)
{
    /* An actual owned lock remains live: fatal handling must abort the whole
     * domain, never convert this worker into an ordinary successful exit. */
    ethr_mutex_lock(argument);
#if CASE == 77
    ETHR_ASSERT(0 && "native-worker-assertion");
#else
    ETHR_FATAL_ERROR__(ENOTSUP);
#endif
    static const char marker[] = "ERTS DIAGNOSTICS unexpected continuation\n";
    knytos_console_write(marker, sizeof(marker) - 1);
    return argument;
}
#endif

int main(void)
{
#if CASE == 76
    int count = 0;
    ETHR_ASSERT(++count);
#ifdef ETHR_DEBUG
    assert(count == 1);
#else
    assert(count == 0);
#endif
    puts("ERTS DIAGNOSTICS assertion macro evaluation PASS");
    /* Keep the finalizer referenced in every profile without installing it
     * on this success path. */
    (void)finalizer;
    return 0;
#else
    assert(atexit(finalizer) == 0);
    ethr_mutex mutex;
    assert(ethr_mutex_init(&mutex) == 0);
    /* A stderr-based failure report would wait behind startup forever.
     * Startup retains the stream lock and exact worker owner through join. */
    flockfile(stderr);
    ethr_tid tid;
    assert(ethr_thr_create(&tid, worker, &mutex, NULL) == 0);
    assert(ethr_thr_join(tid, NULL) == 0);
    funlockfile(stderr);
    abort(); /* Returning or a successful worker exit must fail qualification. */
#endif
}
