/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "knytos_ethread.h"
#include "atomics-values.h"

enum { ROUNDS = 1000, OBSERVATION_LOAD_LIMIT = 1000000 };
#define WORD_READY ((ethr_sint_t)1 << 40)

static struct input {
    ethr_event gate[3];
    ethr_event publishing;
    ethr_atomic32_t count32;
    ethr_atomic_t count_word;
    ethr_atomic32_t ready32;
    ethr_atomic_t ready_word;
    unsigned payload32;
    uint64_t payload_word;
} input;

static void count(void)
{
    for (unsigned i = 0; i < ROUNDS; ++i) {
        ethr_atomic32_inc_relb(&input.count32);
        ethr_atomic_inc_acqb(&input.count_word);
    }
}

static void *counter(void *argument)
{
    ethr_event *gate = argument;
    assert(ethr_event_wait(gate) == 0);
    count();
    return argument;
}

/* Keep two concrete functions for checking the generated AArch64 barriers. */
__attribute__((noinline)) static void publish32(void)
{
    ethr_atomic32_set_relb(&input.ready32, 1);
}

__attribute__((noinline)) static void publish_word(void)
{
    ethr_atomic_set_relb(&input.ready_word, WORD_READY);
}

static void *publisher(void *argument)
{
    /* This notification precedes the payload writes. The observer checks
     * publication through atomics before any join or later notification. */
    ethr_event_set(&input.publishing);
    input.payload32 = 41;
    publish32();
    input.payload_word = UINT64_C(0xfedcba9876543210);
    publish_word();
    return argument;
}

int main(void)
{
    check_atomic_values();
    ethr_atomic32_init(&input.count32, INT32_MAX - 2000);
    ethr_atomic_init(&input.count_word, INTPTR_MAX - 2000);
    ethr_atomic32_init(&input.ready32, 0);
    ethr_atomic_init(&input.ready_word, 0);
    ethr_tid tids[3];
    for (unsigned i = 0; i < 3; ++i) {
        assert(ethr_event_init(&input.gate[i]) == 0);
        assert(ethr_thr_create(&tids[i], counter, &input.gate[i], NULL) == 0);
    }
    for (unsigned i = 0; i < 3; ++i) ethr_event_set(&input.gate[i]);
    count();
    for (unsigned i = 0; i < 3; ++i) {
        void *result = NULL;
        assert(ethr_thr_join(tids[i], &result) == 0 && result == &input.gate[i]);
        assert(ethr_event_destroy(&input.gate[i]) == 0);
    }
    assert(ethr_atomic32_read_mb(&input.count32) == INT32_MIN + 1999);
    assert(ethr_atomic_read_mb(&input.count_word) == INTPTR_MIN + 1999);
    assert(ethr_event_init(&input.publishing) == 0);
    ethr_tid tid;
    assert(ethr_thr_create(&tid, publisher, &input, NULL) == 0);
    assert(ethr_event_wait(&input.publishing) == 0);
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    const uint64_t deadline = now + UINT64_C(1000000000);
    int observed = 0;
    /* An atomic flag has no CQ subscription. This test-only observation is
     * bounded by both a load count and one monotonic deadline, with terminal
     * failure. A post-publication event would mask the ordering under test. */
    for (unsigned i = 0; i < OBSERVATION_LOAD_LIMIT; ++i) {
        if (ethr_atomic32_read_acqb(&input.ready32) == 1
            && ethr_atomic_read_acqb(&input.ready_word) == WORD_READY) {
            observed = 1;
            break;
        }
        if ((i & 255) == 0) {
            assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
            assert(now < deadline);
        }
    }
    assert(observed);
    assert(input.payload32 == 41 && input.payload_word == UINT64_C(0xfedcba9876543210));
    void *result = NULL;
    assert(ethr_thr_join(tid, &result) == 0 && result == &input);
    assert(ethr_event_destroy(&input.publishing) == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS ATOMICS all word and 32-bit variants old/new values and wrapping");
    puts("ERTS ATOMICS four participants contended arithmetic");
    puts("ERTS ATOMICS cross-CPU publication before join");
    puts("ERTS ATOMICS PASS");
    return 0;
}
