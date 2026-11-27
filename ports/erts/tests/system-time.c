/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "erts_system_time.h"

#ifndef CASE
#define CASE 89
#endif

static struct knytos_mutex *mutex;
static struct knytos_event *gate;

static void sample(void *argument)
{
    assert(argument == gate && knytos_event_wait_on(gate, 5000) == KNYTOS_OK);
    for (unsigned i = 0; i < 256; ++i) {
        assert(knytos_mutex_lock(mutex, 5000) == KNYTOS_OK);
        ErtsMonotonicTime before = erts_os_monotonic_time(), mono;
        ErtsSystemTime system;
        erts_os_times(&mono, &system);
        assert(system > 1577836800000000000L);
        assert(before <= mono && mono <= erts_os_monotonic_time());
        assert(erts_os_system_time() > 1577836800000000000L);
        assert(knytos_mutex_unlock(mutex) == KNYTOS_OK);
    }
}

int main(void)
{
    ErtsSystemTime untouched = -123;
    assert(knytos_erts_clock_read(&untouched) == KNYTOS_UNAVAILABLE && untouched == -123);
    if (CASE == 91) {
        (void)erts_os_system_time();
        assert(0);
    }
    assert(knytos_erts_clock_refresh(0) == KNYTOS_INVALID);
    int status = knytos_erts_clock_refresh(UINT64_C(5000000000));
    if (CASE == 90) {
        assert(status == KNYTOS_SERVICE_UNAUTHORIZED);
        assert(knytos_erts_clock_read(&untouched) == KNYTOS_UNAVAILABLE && untouched == -123);
        puts("ERTS SYSTEM TIME missing grant preserves cold state PASS");
        return 0;
    }
    assert(status == KNYTOS_OK);
    struct knytos_realtime_sample prepared;
    assert(knytos_clock_realtime_sample(&prepared) == KNYTOS_OK);
    mutex = knytos_mutex_create();
    gate = knytos_event_create();
    assert(mutex && gate);
    assert(knytos_thread_start(1, 1, sample, gate) == KNYTOS_OK);
    assert(knytos_event_set(gate) == KNYTOS_OK);
    unsigned published = 0, busy = 0;
    for (unsigned i = 0; i < 256; ++i) {
        status = knytos_erts_clock_publish(&prepared, UINT64_C(5000000000));
        if (status == KNYTOS_OK) ++published;
        else { assert(status == KNYTOS_BUSY); ++busy; }
    }
    sample(gate);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(knytos_event_destroy(gate) == KNYTOS_OK);
    assert(knytos_mutex_destroy(mutex) == KNYTOS_OK);
    /* Blocking refresh remains explicit, after all borrowed worker state
     * and native locks have been released. It does not renew the old anchor. */
    assert(knytos_erts_clock_refresh(UINT64_C(5000000000)) == KNYTOS_OK);
    printf("ERTS SYSTEM TIME published=%u busy=%u\n", published, busy);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    puts("ERTS SYSTEM TIME local getters concurrent publication and exact join PASS");
    return 0;
}
