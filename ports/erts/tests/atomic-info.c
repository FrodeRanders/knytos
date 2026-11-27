/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "knytos_ethread.h"

static const char *const scalar_names[] = {
    "cmpxchg_mb", "xchg_mb", "set_mb", "init_mb", "add_return_mb", "read_mb",
    "inc_return_mb", "dec_return_mb", "add_mb", "inc_mb", "dec_mb",
    "and_retold_mb", "or_retold_mb"
};
static const char *const pair_names[] = {"cmpxchg_mb", "set_mb", "read_mb", "init_mb"};

static void names(char **actual, const char *const *expected, unsigned count)
{
    assert(actual != NULL);
    for (unsigned i = 0; i < count; ++i) {
        assert(actual[i] != NULL && strcmp(actual[i], expected[i]) == 0);
        for (unsigned j = 0; j < i; ++j) assert(strcmp(actual[i], actual[j]) != 0);
    }
    assert(actual[count] == NULL);
}

/* Every reported primitive name is callable, including names that differ
 * from the public family (add_return, inc_return and and/or_retold). */
#define CHECK_SCALAR(N, S) \
do { \
    N##_t value; \
    N##_init_mb(&value, 1); \
    assert(N##_read_mb(&value) == 1); \
    N##_set_mb(&value, 2); \
    assert(N##_cmpxchg_mb(&value, 3, 2) == 2); \
    assert(N##_cmpxchg_mb(&value, 99, 2) == 3); \
    assert(N##_xchg_mb(&value, 4) == 3); \
    assert(N##_add_return_mb(&value, 2) == 6); \
    assert(N##_inc_return_mb(&value) == 7); \
    assert(N##_dec_return_mb(&value) == 6); \
    N##_add_mb(&value, 2); \
    N##_inc_mb(&value); \
    N##_dec_mb(&value); \
    assert(N##_and_retold_mb(&value, 3) == 8); \
    assert(N##_or_retold_mb(&value, 5) == 0); \
    assert(N##_read_mb(&value) == 5); \
    assert(*N##_addr(&value) == (S)5); /* Exclusive ordinary interoperability. */ \
} while (0)

static void primitive_contracts(void)
{
    CHECK_SCALAR(ethr_native_atomic32, ethr_sint32_t);
    CHECK_SCALAR(ethr_native_atomic64, ethr_sint64_t);
    _Alignas(16) ethr_native_dw_atomic_t objects[2];
    ethr_sint_t initial[2] = {17, -19}, next[2] = {-23, 29}, expected[2], result[2];
    for (unsigned i = 0; i < 2; ++i) {
        assert(((uintptr_t)&objects[i] & 15) == i * 8);
        ethr_native_dw_atomic_init_mb(&objects[i], initial);
        ethr_native_dw_atomic_read_mb(&objects[i], result);
        assert(memcmp(result, initial, sizeof(result)) == 0);
        memcpy(expected, initial, sizeof(expected));
        assert(ethr_native_dw_atomic_cmpxchg_mb(&objects[i], next, expected) == 1);
        assert(memcmp(expected, initial, sizeof(expected)) == 0);
        assert(ethr_native_dw_atomic_cmpxchg_mb(&objects[i], initial, expected) == 0);
        assert(memcmp(expected, next, sizeof(expected)) == 0);
        ethr_native_dw_atomic_set_mb(&objects[i], initial);
        assert(memcmp(ethr_native_dw_atomic_addr(&objects[i]), initial, sizeof(initial)) == 0);
    }
}

static struct {
    ethr_atomic32_t count;
    char **ops32;
    char **ops64;
    char **ops_dw;
    char **ops_su;
    ethr_event gate[3];
} shared;

static void inventory(void)
{
    assert(ethr_init_atomics() == 0);
    assert(shared.ops32 == ethr_native_atomic32_ops());
    assert(shared.ops64 == ethr_native_atomic64_ops());
    assert(shared.ops_dw == ethr_native_dw_atomic_ops());
    assert(shared.ops_su == ethr_native_su_dw_atomic_ops());
    names(shared.ops32, scalar_names, sizeof(scalar_names) / sizeof(scalar_names[0]));
    names(shared.ops64, scalar_names, sizeof(scalar_names) / sizeof(scalar_names[0]));
    names(shared.ops_dw, pair_names, sizeof(pair_names) / sizeof(pair_names[0]));
    assert(shared.ops_su[0] == NULL); /* No scalar single-unit native API. */
    primitive_contracts();
}

static void *worker(void *argument)
{
    assert(ethr_event_wait(argument) == 0);
    for (unsigned n = 0; n < 32; ++n) {
        inventory();
        ethr_atomic32_inc_mb(&shared.count);
    }
    return argument;
}

int main(void)
{
    /* Tables exist before init, and init may neither reset data nor allocate. */
    shared.ops32 = ethr_native_atomic32_ops();
    shared.ops64 = ethr_native_atomic64_ops();
    shared.ops_dw = ethr_native_dw_atomic_ops();
    shared.ops_su = ethr_native_su_dw_atomic_ops();
    ethr_atomic32_init(&shared.count, 41);
    inventory();
    assert(ethr_atomic32_read(&shared.count) == 41);
    puts("ERTS ATOMIC INFO native names callable and stateless init preserves values");
    ethr_tid tids[3];
    for (unsigned i = 0; i < 3; ++i) {
        assert(ethr_event_init(&shared.gate[i]) == 0);
        assert(ethr_thr_create(&tids[i], worker, &shared.gate[i], NULL) == 0);
    }
    for (unsigned i = 0; i < 3; ++i) ethr_event_set(&shared.gate[i]);
    for (unsigned n = 0; n < 32; ++n) inventory();
    for (unsigned i = 0; i < 3; ++i) {
        void *result = NULL;
        assert(ethr_thr_join(tids[i], &result) == 0 && result == &shared.gate[i]);
        assert(ethr_event_destroy(&shared.gate[i]) == 0);
    }
    assert(ethr_atomic32_read(&shared.count) == 41 + 3 * 32);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS ATOMIC INFO stable borrowed tables concurrent calls and exact joins");
    puts("ERTS ATOMIC INFO PASS");
    return 0;
}
