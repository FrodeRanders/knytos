/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "knytos.h"
#include "erts_monotonic.h"

static struct knytos_mutex *mutex;
static struct knytos_event *gate;

static void sample(void *argument)
{
    assert(argument == gate);
    assert(knytos_event_wait_on(gate, 5000) == KNYTOS_OK);
    ErtsMonotonicTime previous = 0;
    for (unsigned i = 0; i < 256; ++i) {
        /* Exercise the lock context of ERTS time getters. Every iteration
         * samples the clock; this finite loop is not a readiness wait. */
        assert(knytos_mutex_lock(mutex, 5000) == KNYTOS_OK);
        uint64_t before, after;
        assert(knytos_clock_monotonic_ns(&before) == KNYTOS_OK);
        ErtsMonotonicTime now = erts_os_monotonic_time();
        ErtsSysHrTime high = erts_sys_hrtime();
        assert(knytos_clock_monotonic_ns(&after) == KNYTOS_OK);
        assert(now >= previous && now >= 0 && high >= now);
        assert(before <= (uint64_t)now && (uint64_t)high <= after);
        assert(knytos_mutex_unlock(mutex) == KNYTOS_OK);
        previous = now;
    }
}

int main(void)
{
    _Static_assert(ERTS_COMPILE_TIME_MONOTONIC_TIME_UNIT == 1000000000, "nanoseconds");
    _Static_assert(ERTS_HRTIME_UNIT == 1000000000, "nanoseconds");
    mutex = knytos_mutex_create();
    gate = knytos_event_create();
    assert(mutex && gate);
    assert(knytos_thread_start(1, 1, sample, gate) == KNYTOS_OK);
    assert(knytos_event_set(gate) == KNYTOS_OK);
    sample(gate);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(knytos_event_destroy(gate) == KNYTOS_OK);
    assert(knytos_mutex_destroy(mutex) == KNYTOS_OK);
    uint64_t unavailable = 123;
    assert(knytos_clock_realtime_ns(&unavailable) == KNYTOS_UNAVAILABLE && unavailable == 123);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    puts("ERTS MONOTONIC startup and worker locked nanosecond reads PASS");
    return 0;
}
