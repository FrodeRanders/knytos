/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "knytos.h"

#ifndef CASE
#define CASE 84
#endif

struct sample {
    uint64_t value;
    int32_t status;
};

static void sample(void *argument)
{
    struct sample *result = argument;
    result->status = knytos_clock_realtime_ns(&result->value);
}

int main(void)
{
    uint64_t first = 123, second = 456;
    assert(knytos_clock_realtime_ns(NULL) == KNYTOS_INVALID);
    _Alignas(8) unsigned char unaligned[16] = {0};
    assert(knytos_clock_realtime_ns((uint64_t *)(unaligned + 1)) == KNYTOS_INVALID);
    int32_t status = knytos_clock_realtime_ns(&first);
    if (CASE == 85) {
        assert(status == KNYTOS_SERVICE_UNAUTHORIZED && first == 123);
        puts("C REALTIME missing signed grant rejected and output preserved PASS");
        return 0;
    }
    assert(status == KNYTOS_OK && first > UINT64_C(1577836800000000000));
    struct sample worker = {.value = 789};
    assert(knytos_thread_start(1, 1, sample, &worker) == KNYTOS_OK);
    /* Startup and worker query concurrently on their separate private CQs. */
    for (unsigned i = 0; i < 8; ++i)
        assert(knytos_clock_realtime_ns(&second) == KNYTOS_OK && second >= first);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(worker.status == KNYTOS_OK && worker.value >= first);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    puts("C REALTIME synchronized UTC startup and worker private queues PASS");
    return 0;
}
