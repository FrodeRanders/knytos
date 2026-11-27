/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include "knytos_ethread.h"

#if defined(ETHR_INSTRUCTION_BARRIER) || defined(ETHR_ORDERED_READ_DEPEND)
#error "This profile requires explicit data fences and has no instruction barrier"
#endif

/* Retained, separate sites make compiler output independently inspectable. */
#define BARRIER_SITE(NAME, EXPR) \
    __attribute__((noinline)) void knytos_barrier_##NAME(void) { EXPR; }
BARRIER_SITE(full, ETHR_MEMORY_BARRIER)
BARRIER_SITE(write, ETHR_WRITE_MEMORY_BARRIER)
BARRIER_SITE(read, ETHR_READ_MEMORY_BARRIER)
BARRIER_SITE(depend, ETHR_READ_DEPEND_MEMORY_BARRIER)
#define MASK_SITE(N) BARRIER_SITE(mask_##N, ETHR_MEMBAR(N))
MASK_SITE(0)
MASK_SITE(1)
MASK_SITE(2)
MASK_SITE(3)
MASK_SITE(4)
MASK_SITE(5)
MASK_SITE(6)
MASK_SITE(7)
MASK_SITE(8)
MASK_SITE(9)
MASK_SITE(10)
MASK_SITE(11)
MASK_SITE(12)
MASK_SITE(13)
MASK_SITE(14)
MASK_SITE(15)

__attribute__((noinline)) void knytos_barrier_compiler(unsigned *first, unsigned *second)
{
    *first = 41;
    ETHR_COMPILER_BARRIER;
    *second = *first;
}

static void (*const masks[])(void) = {
    knytos_barrier_mask_0, knytos_barrier_mask_1, knytos_barrier_mask_2, knytos_barrier_mask_3,
    knytos_barrier_mask_4, knytos_barrier_mask_5, knytos_barrier_mask_6, knytos_barrier_mask_7,
    knytos_barrier_mask_8, knytos_barrier_mask_9, knytos_barrier_mask_10, knytos_barrier_mask_11,
    knytos_barrier_mask_12, knytos_barrier_mask_13, knytos_barrier_mask_14, knytos_barrier_mask_15
};

enum { ROUNDS = 256, LOAD_LIMIT = 10000000, MODES = 4 };
static struct input {
    ethr_event gate;
    atomic_uint ready;
    atomic_uint acknowledged;
    atomic_uint payload;
    uint64_t deadline;
} input;

/* Atomic flags expose no CQ subscription. This fixture alone uses bounded
 * observation, one shared monotonic deadline and terminal failure. Clock
 * sampling is diagnostic, not notification or production waiting policy. */
static void await(atomic_uint *flag, unsigned wanted)
{
    for (unsigned count = 0; count < LOAD_LIMIT; ++count) {
        if (atomic_load_explicit(flag, memory_order_relaxed) == wanted) return;
        if ((count & 255) == 0) {
            uint64_t now;
            assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK && now < input.deadline);
        }
    }
    assert(0 && "barrier observation load bound");
}

static void publish_barrier(unsigned mode)
{
    switch (mode) {
    case 0: knytos_barrier_full(); break;
    case 1: knytos_barrier_write(); break;
    case 2: knytos_barrier_full(); break;
    default: knytos_barrier_mask_15(); break;
    }
}

static void observe_barrier(unsigned mode)
{
    switch (mode) {
    case 0: knytos_barrier_full(); break;
    case 1: knytos_barrier_read(); break;
    case 2: knytos_barrier_depend(); break;
    default: knytos_barrier_mask_15(); break;
    }
}

static void *publisher(void *argument)
{
    assert(argument == &input && ethr_event_wait(&input.gate) == 0);
    for (unsigned round = 1; round <= ROUNDS * MODES; ++round) {
        unsigned mode = (round - 1) / ROUNDS;
        await(&input.acknowledged, round - 1);
        ETHR_MEMORY_BARRIER;
        atomic_store_explicit(&input.payload, round + 1000 * mode, memory_order_relaxed);
        publish_barrier(mode);
        atomic_store_explicit(&input.ready, round, memory_order_relaxed);
    }
    await(&input.acknowledged, ROUNDS * MODES);
    return argument;
}

int main(void)
{
    for (unsigned i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) masks[i]();
    unsigned evaluations = 0;
    ETHR_MEMBAR(++evaluations);
    assert(evaluations == 1);
    if (evaluations) ETHR_MEMORY_BARRIER;
    else assert(0);
    (ETHR_READ_MEMORY_BARRIER, ++evaluations);
    assert(evaluations == 2);
    unsigned first = 0, second = 0;
    knytos_barrier_compiler(&first, &second);
    assert(first == 41 && second == 41);
    puts("ERTS BARRIERS masks single evaluation expression and compiler ordering");
    atomic_init(&input.ready, 0);
    atomic_init(&input.acknowledged, 0);
    atomic_init(&input.payload, 0);
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    input.deadline = now + UINT64_C(10000000000);
    assert(ethr_event_init(&input.gate) == 0);
    ethr_tid tid;
    assert(ethr_thr_create(&tid, publisher, &input, NULL) == 0);
    ethr_event_set(&input.gate);
    for (unsigned round = 1; round <= ROUNDS * MODES; ++round) {
        unsigned mode = (round - 1) / ROUNDS;
        await(&input.ready, round);
        observe_barrier(mode);
        assert(atomic_load_explicit(&input.payload, memory_order_relaxed) == round + 1000 * mode);
        ETHR_MEMORY_BARRIER;
        atomic_store_explicit(&input.acknowledged, round, memory_order_relaxed);
    }
    void *result = NULL;
    assert(ethr_thr_join(tid, &result) == 0 && result == &input);
    assert(ethr_event_destroy(&input.gate) == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS BARRIERS four publication modes before acknowledgement and exact join");
    puts("ERTS BARRIERS PASS");
    return 0;
}
