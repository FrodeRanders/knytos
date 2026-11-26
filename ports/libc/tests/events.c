#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "knytos.h"

struct shared {
    struct knytos_event *event;
    atomic_uint phase;
    unsigned payload;
};

static uint64_t now_ns(void)
{
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    return now;
}

static void await_phase(struct shared *s, unsigned expected)
{
    uint64_t deadline = now_ns() + 2000000000;
    while (atomic_load_explicit(&s->phase, memory_order_acquire) != expected) {
        uint64_t now = now_ns();
        assert(now < deadline);
        int status = knytos_thread_wait((uint32_t)((deadline - now + 999999) / 1000000));
        assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
    }
}

static void producer(void *argument)
{
    struct shared *s = argument;
    await_phase(s, 1);
    for (unsigned i = 0; i < 10; ++i) {
        assert(knytos_thread_signal(0) == KNYTOS_OK);
        uint64_t until = now_ns() + 30000000;
        uint64_t now;
        /* Fixture-only timed wake injection. Each pause keeps its deadline
         * across unrelated hints on the producer's private queue. */
        while ((now = now_ns()) < until) {
            int status = knytos_thread_wait((uint32_t)((until - now + 999999) / 1000000));
            assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
        }
    }
    await_phase(s, 2);
    s->payload = 42;
    assert(knytos_event_set(s->event) == KNYTOS_OK);
}

static void waiter(void *argument)
{
    struct shared *s = argument;
    assert(knytos_event_wait_on(s->event, -1) == KNYTOS_OK);
    assert(s->payload == 43);
}

int main(void)
{
    struct shared s = { .event = knytos_event_create() };
    assert(s.event);
    assert(knytos_event_wait_on(NULL, 0) == KNYTOS_INVALID);
    assert(knytos_event_wait_on(s.event, -2) == KNYTOS_INVALID);
    assert(knytos_event_wait_on(s.event, 0) == KNYTOS_DEADLINE);
    assert(knytos_event_set(s.event) == KNYTOS_OK);
    assert(knytos_event_set(s.event) == KNYTOS_OK);
    assert(knytos_event_wait_on(s.event, 0) == KNYTOS_OK);
    assert(knytos_event_wait_on(s.event, 0) == KNYTOS_OK);
    assert(knytos_event_reset(s.event) == KNYTOS_OK);
    assert(knytos_event_wait_on(s.event, 0) == KNYTOS_DEADLINE);
    puts("EVENT retained set reset and wake-before-wait");

    assert(knytos_thread_start(1, 1, producer, &s) == KNYTOS_OK);
    atomic_store_explicit(&s.phase, 1, memory_order_release);
    assert(knytos_thread_signal(1) == KNYTOS_OK);
    uint64_t started = now_ns();
    assert(knytos_event_wait_on(s.event, 100) == KNYTOS_DEADLINE);
    uint64_t elapsed = now_ns() - started;
    assert(elapsed >= 100000000 && elapsed < 220000000);
    atomic_store_explicit(&s.phase, 2, memory_order_release);
    assert(knytos_thread_signal(1) == KNYTOS_OK);
    assert(knytos_event_wait_on(s.event, 2000) == KNYTOS_OK);
    assert(s.payload == 42);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    printf("EVENT unrelated wakes preserve deadline elapsed_ns=%llu\n",
        (unsigned long long)elapsed);

    assert(knytos_event_reset(s.event) == KNYTOS_OK);
    s.payload = 43;
    assert(knytos_thread_start(1, 1, waiter, &s) == KNYTOS_OK);
    assert(knytos_event_set(s.event) == KNYTOS_OK);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(knytos_event_reset(s.event) == KNYTOS_OK);
    assert(knytos_event_wait_on(s.event, 1) == KNYTOS_DEADLINE);
    assert(knytos_event_destroy(s.event) == KNYTOS_OK);
    puts("EVENT cross-CPU publication and event-only wait");
    puts("EVENT PASS");
    return 0;
}
