/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "knytos.h"

#ifndef CASE
#define CASE 87
#endif

_Static_assert(sizeof(struct knytos_realtime_sample) == 48, "sample size");
_Static_assert(_Alignof(struct knytos_realtime_sample) == 8, "sample alignment");
_Static_assert(offsetof(struct knytos_realtime_sample, drift_ppb) == 24, "sample rate");
_Static_assert(offsetof(struct knytos_realtime_sample, last_sync_age_ms) == 32, "sample NTP age");
_Static_assert(offsetof(struct knytos_realtime_sample, uncertainty_ms) == 40, "sample uncertainty");
_Static_assert(offsetof(struct knytos_realtime_sample, reserved) == 44, "sample reserved");

static struct input {
    struct knytos_realtime_sample sample;
    struct knytos_mutex *mutex;
    struct knytos_event *gate;
    uint64_t previous;
} input;

static void project(void *argument)
{
    assert(argument == &input);
    assert(knytos_event_wait_on(input.gate, 5000) == KNYTOS_OK);
    /* A finite sampling fixture, not a readiness wait. No refresh or RPC
     * occurs under this mutex. The common sample remains immutable. */
    for (unsigned i = 0; i < 256; ++i) {
        assert(knytos_mutex_lock(input.mutex, 5000) == KNYTOS_OK);
        uint64_t now;
        assert(knytos_clock_sample_project_ns(&input.sample, UINT64_C(5000000000), &now) == KNYTOS_OK);
        assert(now >= input.previous && now >= input.sample.utc_ns);
        input.previous = now;
        assert(knytos_mutex_unlock(input.mutex) == KNYTOS_OK);
    }
}

static void rejected(struct knytos_realtime_sample sample, uint64_t limit, int32_t expected)
{
    uint64_t output = 123;
    assert(knytos_clock_sample_project_ns(&sample, limit, &output) == expected && output == 123);
}

int main(void)
{
    struct knytos_realtime_sample sample, untouched;
    memset(&sample, 0xa5, sizeof(sample));
    untouched = sample;
    _Alignas(8) unsigned char unaligned[64] = {0};
    assert(knytos_clock_realtime_sample(NULL) == KNYTOS_INVALID);
    assert(knytos_clock_realtime_sample((void *)(unaligned + 1)) == KNYTOS_INVALID);
    uint64_t output = 123;
    assert(knytos_clock_sample_project_ns(NULL, 0, &output) == KNYTOS_INVALID && output == 123);
    assert(knytos_clock_sample_project_ns((void *)(unaligned + 1), 0, &output) == KNYTOS_INVALID);
    assert(knytos_clock_sample_project_ns(&sample, 0, NULL) == KNYTOS_INVALID);
    assert(knytos_clock_sample_project_ns(&sample, 0, (void *)(unaligned + 1)) == KNYTOS_INVALID);
    uint64_t before, after;
    assert(knytos_clock_monotonic_ns(&before) == KNYTOS_OK);
    int32_t status = knytos_clock_realtime_sample(&sample);
    if (CASE == 88) {
        assert(status == KNYTOS_SERVICE_UNAUTHORIZED && memcmp(&sample, &untouched, sizeof(sample)) == 0);
        puts("C REALTIME SAMPLE missing signed grant preserves complete output PASS");
        return 0;
    }
    assert(status == KNYTOS_OK && sample.utc_ns > UINT64_C(1577836800000000000));
    assert(sample.counter_frequency_hz && sample.reserved == 0 && sample.last_sync_age_ms != UINT64_MAX);
    assert(knytos_clock_monotonic_ns(&after) == KNYTOS_OK);
    uint64_t anchor_ns = (uint64_t)((__uint128_t)sample.monotonic_ticks * 1000000000 / sample.counter_frequency_hz);
    assert(before <= anchor_ns && anchor_ns <= after);
    input.sample = sample;
    input.previous = sample.utc_ns;
    input.mutex = knytos_mutex_create();
    input.gate = knytos_event_create();
    assert(input.mutex && input.gate);
    assert(knytos_thread_start(1, 1, project, &input) == KNYTOS_OK);
    assert(knytos_event_set(input.gate) == KNYTOS_OK);
    project(&input);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(knytos_event_destroy(input.gate) == KNYTOS_OK);
    assert(knytos_mutex_destroy(input.mutex) == KNYTOS_OK);

    sample.monotonic_ticks = 0;
    rejected(sample, 0, KNYTOS_DEADLINE);
    sample.utc_ns = UINT64_MAX;
    rejected(sample, UINT64_MAX, KNYTOS_OVERFLOW);
    sample = input.sample;
    sample.monotonic_ticks = UINT64_MAX;
    rejected(sample, UINT64_MAX, KNYTOS_UNAVAILABLE);
    sample = input.sample;
    sample.counter_frequency_hz = sample.counter_frequency_hz == 1 ? 2 : 1;
    rejected(sample, UINT64_MAX, KNYTOS_UNAVAILABLE);
    sample = input.sample;
    sample.reserved = 1;
    rejected(sample, UINT64_MAX, KNYTOS_INVALID);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    puts("C REALTIME SAMPLE bounded local projection startup and worker locked reads PASS");
    return 0;
}
