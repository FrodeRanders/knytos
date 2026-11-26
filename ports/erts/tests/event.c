/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>

/* Compile the adapter into this standalone qualification executable using
 * the ordinary C build entry point. ERTS itself is not linked by this test. */
#include "../ethr_event.c"

#ifndef CASE
#define CASE 40
#endif

struct shared {
    ethr_event event;
    unsigned payload;
};

static uint64_t now_ns(void)
{
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    return now;
}

static void producer(void *argument)
{
    struct shared *s = argument;
    uint64_t until = now_ns() + 100000000;
    uint64_t now;
    while ((now = now_ns()) < until) {
        int status = knytos_thread_wait((uint32_t)((until - now + 999999) / 1000000));
        assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
    }
    s->payload = 42;
    ethr_event_set(&s->event);
}

int main(void)
{
    struct shared s = {0};
    assert(ethr_event_init(&s.event) == 0);
    if (CASE == 41) {
        ethr_event_stwait(&s.event, -1, 0);
        return 1;
    }
    assert(ethr_event_prepare_timed(&s.event) == 0);
    assert(ethr_event_twait(&s.event, 0) == ETIMEDOUT);
    uint64_t start = now_ns();
    assert(ethr_event_twait(&s.event, 1) == ETIMEDOUT);
    assert(now_ns() - start >= 1000000);
    puts("ERTS EVENT nanosecond timeout rounds up");

    ethr_event_set(&s.event);
    assert(ethr_event_wait(&s.event) == 0);
    assert(ethr_event_swait(&s.event, 100) == 0);
    assert(ethr_event_stwait(&s.event, 100, INT64_MAX) == 0);
    assert(ethr_event_twait(&s.event, INT64_MIN) == 0);
    ethr_event_reset(&s.event);
    assert(ethr_event_twait(&s.event, 0) == ETIMEDOUT);
    puts("ERTS EVENT retained state and signed timeout bounds");

    assert(knytos_thread_start(1, 1, producer, &s) == KNYTOS_OK);
    assert(ethr_event_wait(&s.event) == 0);
    assert(s.payload == 42);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(ethr_event_destroy(&s.event) == 0);
    assert(ethr_event_prepare_timed(&s.event) == EINVAL);
    assert(ethr_event_destroy(&s.event) == EINVAL);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS EVENT cross-CPU wait and destruction");
    puts("ERTS EVENT PASS");
    return 0;
}
