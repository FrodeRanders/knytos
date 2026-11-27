/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include "knytos_ethread.h"
#include "dw-atomics-values.h"

enum { WORKERS = 3, ROUNDS = 512, STORES = 2048, LOAD_LIMIT = 1000000 };
#define PUBLISHED (INTPTR_MAX - 11)
static struct input {
    _Alignas(16) ethr_dw_atomic_t objects[2];
    ethr_event start[WORKERS];
    ethr_event counted[WORKERS];
    ethr_event store[WORKERS];
    atomic_uint completed;
    uint64_t deadline;
    uint64_t payload;
} input;

/* Keep native instruction sites inspectable in the linked fixture. */
__attribute__((noinline)) void knytos_dw_set(ethr_dw_atomic_t *object, ethr_dw_sint_t *value)
{
    ethr_dw_atomic_set_relb(object, value);
}

__attribute__((noinline)) void knytos_dw_read(ethr_dw_atomic_t *object, ethr_dw_sint_t *value)
{
    ethr_dw_atomic_read_acqb(object, value);
}

__attribute__((noinline)) int knytos_dw_cas(ethr_dw_atomic_t *object, ethr_dw_sint_t *next,
                                          ethr_dw_sint_t *expected)
{
    return ethr_dw_atomic_cmpxchg_mb(object, next, expected);
}

static void budget(unsigned attempts)
{
    assert(attempts < LOAD_LIMIT);
    if ((attempts & 255) == 0) {
        uint64_t now;
        assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK && now < input.deadline);
    }
}

static ethr_dw_sint_t pair(ethr_sint_t number)
{
    return (ethr_dw_sint_t){.sint = {number, ~number}};
}

static void valid(ethr_dw_sint_t value)
{
    assert(value.sint[1] == ~value.sint[0]);
}

static void count(void)
{
    ethr_dw_atomic_t *object = &input.objects[1];
    for (unsigned round = 0; round < ROUNDS; ++round) {
        ethr_dw_sint_t expected;
        knytos_dw_read(object, &expected);
        for (unsigned attempts = 0;; ++attempts) {
            budget(attempts);
            valid(expected);
            assert(expected.sint[0] >= 0 && expected.sint[0] < (WORKERS + 1) * ROUNDS);
            ethr_dw_sint_t next = pair(expected.sint[0] + 1);
            ethr_dw_sint_t before = expected;
            if (knytos_dw_cas(object, &next, &expected)) {
                assert(dw_equal(expected, before));
                break;
            }
            valid(expected); /* Failure must copy one coherent observed pair. */
        }
    }
}

static void *worker(void *argument)
{
    unsigned index = *(unsigned *)argument;
    assert(ethr_event_wait(&input.start[index]) == 0);
    count();
    ethr_event_set(&input.counted[index]);
    assert(ethr_event_wait(&input.store[index]) == 0);
    for (unsigned n = 0; n < STORES; ++n) {
        ethr_dw_sint_t value = pair(1 + index * STORES + n);
        knytos_dw_set(&input.objects[1], &value);
    }
    atomic_fetch_add_explicit(&input.completed, 1, memory_order_release);
    if (index == 0) {
        for (unsigned attempts = 0; atomic_load_explicit(&input.completed, memory_order_relaxed) != WORKERS; ++attempts)
            budget(attempts);
        /* Completion counting precedes these writes. There is no later event
         * or join before startup checks publication through the pair. */
        input.payload = UINT64_C(0xfedcba9876543210);
        ethr_dw_sint_t published = pair(PUBLISHED);
        knytos_dw_set(&input.objects[1], &published);
    }
    return argument;
}

int main(void)
{
    check_dw_values();
    puts("ERTS DW all variants compare both halves expected-value and eight-byte alignment");
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    input.deadline = now + UINT64_C(10000000000);
    ethr_dw_sint_t initial = pair(0);
    ethr_dw_atomic_init(&input.objects[1], &initial);
    atomic_init(&input.completed, 0);
    unsigned indices[WORKERS];
    ethr_tid tids[WORKERS];
    for (unsigned i = 0; i < WORKERS; ++i) {
        indices[i] = i;
        assert(ethr_event_init(&input.start[i]) == 0);
        assert(ethr_event_init(&input.counted[i]) == 0);
        assert(ethr_event_init(&input.store[i]) == 0);
        assert(ethr_thr_create(&tids[i], worker, &indices[i], NULL) == 0);
    }
    for (unsigned i = 0; i < WORKERS; ++i) ethr_event_set(&input.start[i]);
    count();
    for (unsigned i = 0; i < WORKERS; ++i) assert(ethr_event_wait(&input.counted[i]) == 0);
    ethr_dw_sint_t observed;
    knytos_dw_read(&input.objects[1], &observed);
    assert(dw_equal(observed, pair((WORKERS + 1) * ROUNDS)));
    puts("ERTS DW four participants contended compare exchange");
    /* Phase hints prove the earlier CAS loops stopped, not that event setters
     * or callbacks ended. All events, arguments and workers remain retained. */
    for (unsigned i = 0; i < WORKERS; ++i) ethr_event_set(&input.store[i]);
    unsigned reads = 0;
    do {
        budget(reads++);
        knytos_dw_read(&input.objects[1], &observed);
        valid(observed);
    } while (atomic_load_explicit(&input.completed, memory_order_relaxed) != WORKERS || reads < 1000);
    /* Pair flags expose no CQ subscription. This test-only bounded observation
     * shares the original deadline; native atomic retries are lock-free, not
     * wait-free. Production resource waiting still needs notifications. */
    for (unsigned attempts = 0;; ++attempts) {
        budget(attempts);
        knytos_dw_read(&input.objects[1], &observed);
        valid(observed);
        if (observed.sint[0] == PUBLISHED) break;
    }
    assert(input.payload == UINT64_C(0xfedcba9876543210));
    for (unsigned i = 0; i < WORKERS; ++i) {
        void *result = NULL;
        assert(ethr_thr_join(tids[i], &result) == 0 && result == &indices[i]);
        assert(ethr_event_destroy(&input.start[i]) == 0);
        assert(ethr_event_destroy(&input.counted[i]) == 0);
        assert(ethr_event_destroy(&input.store[i]) == 0);
    }
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS DW coherent set read and publication before exact joins");
    puts("ERTS DW PASS");
    return 0;
}
