#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "knytos.h"

#ifndef CASE
#define CASE 14
#endif

struct handshake {
    atomic_int phase;
    atomic_int result;
};

/* Private queue wakes can coalesce or arrive early. A wake is never itself
 * an answer to a handshake and does not restart the deadline. */
static int await_phase(struct handshake *shared, int expected)
{
    uint64_t now;
    int status = knytos_clock_monotonic_ns(&now);
    if (status != KNYTOS_OK)
        return status;
    const uint64_t deadline = now + 2000000000;
    while (atomic_load_explicit(&shared->phase, memory_order_acquire) != expected) {
        status = knytos_clock_monotonic_ns(&now);
        if (status != KNYTOS_OK)
            return status;
        if (now >= deadline)
            return KNYTOS_DEADLINE;
        status = knytos_thread_wait((uint32_t)((deadline - now + 999999) / 1000000));
        if (status != KNYTOS_OK && status != KNYTOS_DEADLINE)
            return status;
    }
    return KNYTOS_OK;
}

static void exchange(void *argument)
{
    struct handshake *shared = argument;
    static const char entered[] = "WORKER private queue entry\n";
    int status = knytos_console_write(entered, sizeof(entered) - 1);
    if (status != KNYTOS_OK)
        goto publish_error;
    status = knytos_thread_wait(0);
    if (status != KNYTOS_OK && status != KNYTOS_DEADLINE)
        goto publish_error;

    /* This phase confirms the worker entered its private CQ API. */
    atomic_store_explicit(&shared->phase, 1, memory_order_release);
    status = knytos_thread_signal(0);
    if (status == KNYTOS_OK)
        status = await_phase(shared, 2);
    if (status == KNYTOS_OK)
        status = knytos_thread_signal(0);

publish_error:
    atomic_store_explicit(&shared->result, status, memory_order_release);
}

static void never_admitted(void *argument)
{
    struct handshake *shared = argument;
    atomic_store_explicit(&shared->result, KNYTOS_UNAVAILABLE, memory_order_release);
}

static void return_immediately(void *argument)
{
    struct handshake *shared = argument;
    atomic_store_explicit(&shared->phase, 3, memory_order_release);
}

static void finalizer(void)
{
    static struct handshake retained;
    assert(knytos_thread_start(1, 1, never_admitted, &retained) == KNYTOS_SHUTDOWN);
    puts("WORKER finalizer admission fenced");
}

int main(void)
{
    if (CASE == 15) {
        static struct handshake retained;
        assert(knytos_thread_start(1, 1, exchange, &retained) == KNYTOS_OK);
        puts("WORKER expected unjoined exit");
        return 0;
    }
    struct handshake shared = { 0 };
    if (CASE == 16) {
        /* The descriptor leaves capacity for every generation even if its
         * stack is still charged after the worker's exit receipt. */
        for (unsigned i = 0; i < 8; ++i) {
            atomic_store_explicit(&shared.phase, 0, memory_order_relaxed);
            assert(knytos_thread_start(1, 1, return_immediately, &shared) == KNYTOS_OK);
            assert(knytos_thread_join(1) == KNYTOS_OK);
            assert(atomic_load_explicit(&shared.phase, memory_order_acquire) == 3);
        }
        assert(atexit(finalizer) == 0);
        puts("WORKER exact join and slot reuse");
        return 0;
    }
    assert(knytos_thread_wait(4) == KNYTOS_DEADLINE);
    assert(knytos_thread_signal(0) == KNYTOS_OK);
    assert(knytos_thread_wait(0) == KNYTOS_OK);
    assert(knytos_thread_wait(0) == KNYTOS_DEADLINE);
    puts("WORKER wake-before-wait and deadline");

    assert(knytos_thread_start(1, UINT32_MAX, never_admitted, &shared) == KNYTOS_UNAVAILABLE);
    assert(knytos_thread_join(1) == KNYTOS_INVALID);
    assert(knytos_thread_start(1, 1, exchange, &shared) == KNYTOS_OK);
    assert(knytos_thread_start(1, 1, never_admitted, &shared) == KNYTOS_BUSY);
    /* The signed fixture permits two threads including startup. */
    assert(knytos_thread_start(2, 1, never_admitted, &shared) == KNYTOS_UNAVAILABLE);
    assert(knytos_thread_join(2) == KNYTOS_INVALID);
    assert(await_phase(&shared, 1) == KNYTOS_OK);
    atomic_store_explicit(&shared.phase, 2, memory_order_release);
    assert(knytos_thread_signal(1) == KNYTOS_OK);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(atomic_load_explicit(&shared.result, memory_order_acquire) == KNYTOS_OK);
    assert(knytos_thread_signal(1) == KNYTOS_INVALID);
    puts("WORKER cross-CPU handshake and admission");

    puts("WORKER PASS");
    return 0;
}
