#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "knytos.h"

struct shared {
    struct knytos_mutex *mutex;
    struct knytos_condition *condition;
    atomic_uint progress;
    atomic_uint returned;
    atomic_uint failed;
    unsigned payload;
};

/* Wake delivery is only a reason to check this predicate. */
static void await_count(atomic_uint *value, unsigned minimum)
{
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    const uint64_t deadline = now + 2000000000;
    while (atomic_load_explicit(value, memory_order_acquire) < minimum) {
        assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
        assert(now < deadline);
        int status = knytos_thread_wait((uint32_t)((deadline - now + 999999) / 1000000));
        assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
    }
}

static void report_return(struct shared *shared, int success)
{
    if (!success)
        atomic_fetch_add_explicit(&shared->failed, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&shared->returned, 1, memory_order_acq_rel);
    knytos_thread_signal(0);
}

static void contended(void *argument)
{
    struct shared *shared = argument;
    int busy = knytos_mutex_try_lock(shared->mutex);
    int timed = knytos_mutex_lock(shared->mutex, 15);
    if (busy != KNYTOS_BUSY || timed != KNYTOS_DEADLINE) {
        if (busy == KNYTOS_OK || timed == KNYTOS_OK)
            knytos_mutex_unlock(shared->mutex);
        report_return(shared, 0);
        return;
    }
    atomic_store_explicit(&shared->progress, 1, memory_order_release);
    knytos_thread_signal(0);
    if (knytos_mutex_lock(shared->mutex, 2000) != KNYTOS_OK) {
        report_return(shared, 0);
        return;
    }
    int valid = shared->payload == 41;
    knytos_mutex_unlock(shared->mutex);
    report_return(shared, valid);
}

static void condition_worker(void *argument)
{
    struct shared *shared = argument;
    if (knytos_mutex_lock(shared->mutex, 2000) != KNYTOS_OK) {
        report_return(shared, 0);
        return;
    }
    atomic_fetch_add_explicit(&shared->progress, 1, memory_order_acq_rel);
    knytos_thread_signal(0);
    int status = knytos_condition_wait(shared->condition, shared->mutex, 2000);
    if (status != KNYTOS_OK) {
        report_return(shared, 0);
        return;
    }
    int valid = shared->payload == 42 && knytos_mutex_try_lock(shared->mutex) == KNYTOS_INVALID;
    knytos_mutex_unlock(shared->mutex);
    report_return(shared, valid);
}

static void relock_timeout(void *argument)
{
    struct shared *shared = argument;
    if (knytos_mutex_lock(shared->mutex, 2000) != KNYTOS_OK) {
        report_return(shared, 0);
        return;
    }
    atomic_store_explicit(&shared->progress, 1, memory_order_release);
    knytos_thread_signal(0);
    uint64_t started, finished;
    int clock_ok = knytos_clock_monotonic_ns(&started) == KNYTOS_OK;
    int status = knytos_condition_wait(shared->condition, shared->mutex, 450);
    clock_ok = clock_ok && knytos_clock_monotonic_ns(&finished) == KNYTOS_OK;
    int unlocked = knytos_mutex_unlock(shared->mutex) == KNYTOS_INVALID;
    /* The producer spends 300 ms of this deadline before signaling. A new
     * 450 ms reacquisition budget would exceed this test's margin. */
    report_return(shared, status == KNYTOS_DEADLINE && unlocked && clock_ok
        && finished - started >= 450000000 && finished - started < 650000000);
}

static void delay_on_default_queue(uint32_t timeout_ms)
{
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    const uint64_t deadline = now + (uint64_t)timeout_ms * 1000000;
    do {
        int status = knytos_event_wait((int64_t)((deadline - now + 999999) / 1000000));
        assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
        assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    } while (now < deadline);
}

static void abandon_mutex(void *argument)
{
    struct shared *shared = argument;
    report_return(shared, knytos_mutex_lock(shared->mutex, 2000) == KNYTOS_OK);
}

static void check_previous_owner(void *argument)
{
    struct shared *shared = argument;
    int rejected = knytos_mutex_unlock(shared->mutex) == KNYTOS_INVALID;
    report_return(shared, rejected && knytos_mutex_lock(shared->mutex, 5) == KNYTOS_DEADLINE);
}

int main(void)
{
    struct shared shared = {
        .mutex = knytos_mutex_create(),
        .condition = knytos_condition_create(),
    };
    assert(shared.mutex && shared.condition);
    assert(knytos_mutex_try_lock(shared.mutex) == KNYTOS_OK);
    assert(knytos_mutex_try_lock(shared.mutex) == KNYTOS_INVALID);
    assert(knytos_mutex_destroy(shared.mutex) == KNYTOS_BUSY);
    assert(knytos_condition_signal(shared.condition, 0) == KNYTOS_OK);
    assert(knytos_condition_wait(shared.condition, shared.mutex, 5) == KNYTOS_DEADLINE);
    assert(knytos_mutex_unlock(shared.mutex) == KNYTOS_INVALID);
    assert(knytos_mutex_try_lock(shared.mutex) == KNYTOS_OK);

    assert(knytos_thread_start(1, 1, contended, &shared) == KNYTOS_OK);
    await_count(&shared.progress, 1);
    shared.payload = 41;
    assert(knytos_mutex_unlock(shared.mutex) == KNYTOS_OK);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(atomic_load(&shared.failed) == 0 && atomic_load(&shared.returned) == 1);
    puts("SYNC mutex contention and timeout");

    for (unsigned broadcast = 0; broadcast <= 1; ++broadcast) {
        atomic_store(&shared.progress, 0);
        atomic_store(&shared.returned, 0);
        assert(knytos_thread_start(1, 1, condition_worker, &shared) == KNYTOS_OK);
        assert(knytos_thread_start(2, 1, condition_worker, &shared) == KNYTOS_OK);
        await_count(&shared.progress, 2);
        assert(knytos_mutex_lock(shared.mutex, 2000) == KNYTOS_OK);
        /* Both workers have released the mutex through condition registration.
         * An unrelated CQ hint to the other waiter must not select it. */
        assert(knytos_thread_signal(2) == KNYTOS_OK);
        shared.payload = 42;
        assert(knytos_condition_signal(shared.condition, broadcast) == KNYTOS_OK);
        assert(atomic_load(&shared.returned) == 0);
        assert(knytos_mutex_unlock(shared.mutex) == KNYTOS_OK);
        if (!broadcast) {
            await_count(&shared.returned, 1);
            assert(atomic_load(&shared.returned) == 1);
            assert(knytos_condition_signal(shared.condition, 1) == KNYTOS_OK);
        }
        assert(knytos_thread_join(1) == KNYTOS_OK);
        assert(knytos_thread_join(2) == KNYTOS_OK);
        assert(atomic_load(&shared.failed) == 0 && atomic_load(&shared.returned) == 2);
    }
    puts("SYNC condition signal and broadcast");

    atomic_store(&shared.progress, 0);
    atomic_store(&shared.returned, 0);
    assert(knytos_thread_start(1, 1, relock_timeout, &shared) == KNYTOS_OK);
    await_count(&shared.progress, 1);
    assert(knytos_mutex_lock(shared.mutex, 2000) == KNYTOS_OK);
    delay_on_default_queue(300);
    assert(atomic_load(&shared.returned) == 0);
    assert(knytos_condition_signal(shared.condition, 0) == KNYTOS_OK);
    /* Hold the lock until the selected waiter exhausts its original budget. */
    await_count(&shared.returned, 1);
    assert(knytos_mutex_unlock(shared.mutex) == KNYTOS_OK);
    assert(knytos_thread_join(1) == KNYTOS_OK && atomic_load(&shared.failed) == 0);
    assert(knytos_condition_destroy(shared.condition) == KNYTOS_OK);
    assert(knytos_mutex_destroy(shared.mutex) == KNYTOS_OK);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK);
    assert(allocations.blocks == 0);
    puts("SYNC one deadline through reacquisition");

    /* Misuse probe: neither a replacement worker nor the startup thread
     * owns a lock left held by the previous worker in slot 1. */
    shared.mutex = knytos_mutex_create();
    assert(shared.mutex);
    assert(knytos_thread_start(1, 1, abandon_mutex, &shared) == KNYTOS_OK);
    assert(knytos_thread_join(1) == KNYTOS_OK && atomic_load(&shared.failed) == 0);
    assert(knytos_thread_start(1, 1, check_previous_owner, &shared) == KNYTOS_OK);
    assert(knytos_thread_join(1) == KNYTOS_OK && atomic_load(&shared.failed) == 0);
    assert(knytos_mutex_destroy(shared.mutex) == KNYTOS_BUSY);
    /* Terminal disposal reclaims the plain metadata after every join. */
    puts("SYNC previous slot owner fenced");
    puts("SYNC PASS");
    return 0;
}
