/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knytos_ethread.h"

#ifndef CASE
#define CASE 59
#endif

static struct input {
    ethr_event gate;
    ethr_tid tid;
    unsigned index;
    unsigned copied;
    unsigned cleaned;
} inputs[3];
static uint64_t context_key;
static unsigned prepare_calls;
static atomic_uint parent_calls;
static atomic_uint child_calls;

struct prepared {
    ethr_event delay;
    struct input *input;
    atomic_int child_done;
    unsigned value;
};

static void *entry(void *argument);

static void reject_reentrant_control(void)
{
    ethr_tid unchanged = {99, 99};
    void *result = &unchanged;
    assert(ethr_thr_create(&unchanged, entry, NULL, NULL) == EBUSY);
    assert(unchanged.slot == 99 && unchanged.generation == 99);
    assert(ethr_thr_join(unchanged, &result) == EBUSY && result == &unchanged);
    assert(ethr_thr_detach(unchanged) == EBUSY);
    assert(knytos_ethr_threads_shutdown() == EBUSY);
    assert(knytos_ethr_threads_init(NULL) == EBUSY);
}

static void *prepare(void)
{
    assert(knytos_thread_is_startup());
    reject_reentrant_control();
    struct prepared *prepared = calloc(1, sizeof(*prepared));
    assert(prepared);
    prepared->input = &inputs[prepare_calls];
    prepared->value = 41 + prepare_calls;
    assert(ethr_event_init(&prepared->delay) == 0);
    ++prepare_calls;
    return prepared;
}

static void child_init(void *argument)
{
    struct prepared *prepared = argument;
    struct input *input = prepared->input;
    assert(!knytos_thread_is_startup());
    assert(knytos_ethr_threads_init(NULL) == EACCES);
    ethr_tid tid = ethr_self();
    assert(tid.slot && tid.generation);
    char name[ETHR_THR_NAME_MAX + 1];
    assert(ethr_getname(tid, name, sizeof(name)) == 0);
    assert(strcmp(name, "hook-worker") == 0);
    if (CASE == 61) ethr_thr_exit(NULL);
    assert(knytos_tsd_set(context_key, input) == KNYTOS_OK);
    /* Inject an unrelated startup wake before child initialization finishes. */
    assert(knytos_thread_signal(0) == KNYTOS_OK);
    assert(ethr_event_twait(&prepared->delay, 10000000) == ETIMEDOUT);
    assert(atomic_load(&parent_calls) == input->index);
    input->copied = prepared->value;
    input->tid = tid;
    atomic_fetch_add(&child_calls, 1);
    atomic_store_explicit(&prepared->child_done, 1, memory_order_release);
}

static void parent_init(void *argument)
{
    struct prepared *prepared = argument;
    assert(knytos_thread_is_startup());
    reject_reentrant_control();
    assert(atomic_load_explicit(&prepared->child_done, memory_order_acquire) == (CASE != 60));
    if (CASE != 60) assert(prepared->input->copied == prepared->value);
    assert(ethr_event_destroy(&prepared->delay) == 0);
    free(prepared);
    atomic_fetch_add(&parent_calls, 1);
}

static void cleanup(void *argument)
{
    struct input *input = argument;
    assert(ethr_equal_tids(ethr_self(), input->tid));
    assert(atomic_load(&parent_calls) >= input->index + 1);
    ++input->cleaned;
}

static void *entry(void *argument)
{
    struct input *input = argument;
    assert(knytos_tsd_get(context_key) == input);
    assert(input->copied == 41 + input->index);
    assert(ethr_event_wait(&input->gate) == 0);
    if (input->index == 1) ethr_thr_exit(input);
    return input;
}

int main(void)
{
    ethr_tid unchanged = {99, 99};
    assert(ethr_thr_create(NULL, entry, NULL, NULL) == EINVAL);
    ethr_init_data data = ETHR_INIT_DATA_DEFAULT_INITER;
    data.thread_create_prepare_func = prepare;
    data.thread_create_parent_func = parent_init;
    data.thread_create_child_func = child_init;
    assert(knytos_ethr_threads_init(&data) == 0);
    data = (ethr_init_data) ETHR_INIT_DATA_DEFAULT_INITER;
    assert(knytos_ethr_threads_init(&data) == EBUSY);
    assert(prepare_calls == 0 && atomic_load(&parent_calls) == 0);
    assert(knytos_tsd_key_create_with_destructor(&context_key, cleanup) == KNYTOS_OK);
    for (unsigned i = 0; i < 3; ++i) {
        inputs[i].index = i;
        assert(ethr_event_init(&inputs[i].gate) == 0);
    }
    ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
    options.name = "hook-worker";
    if (CASE == 60) {
        assert(ethr_thr_create(&unchanged, entry, &inputs[0], &options) == EAGAIN);
        assert(unchanged.slot == 99 && unchanged.generation == 99);
        assert(prepare_calls == 1 && atomic_load(&parent_calls) == 1);
        assert(atomic_load(&child_calls) == 0);
        puts("ERTS INIT rejected start parent cleanup and fenced reuse");
    } else {
        for (unsigned i = 0; i < 3; ++i) {
            options.detached = i == 2;
            ethr_tid tid;
            assert(ethr_thr_create(&tid, entry, &inputs[i], &options) == 0);
            assert(ethr_equal_tids(tid, inputs[i].tid));
            assert(prepare_calls == i + 1 && atomic_load(&parent_calls) == i + 1);
            ethr_event_set(&inputs[i].gate);
            if (i != 2) {
                void *result = NULL;
                assert(ethr_thr_join(tid, &result) == 0 && result == &inputs[i]);
                assert(inputs[i].cleaned == 1);
            }
        }
        assert(atomic_load(&child_calls) == 3);
        puts("ERTS INIT copied hooks and child-before-parent handshake");
    }
    assert(knytos_ethr_threads_shutdown() == 0);
    assert(knytos_ethr_threads_init(NULL) == ECANCELED);
    assert(ethr_thr_create(&unchanged, entry, NULL, NULL) == ECANCELED);
    for (unsigned i = 0; i < 3; ++i) {
        assert(inputs[i].cleaned == (CASE != 60));
        assert(ethr_event_destroy(&inputs[i].gate) == 0);
    }
    assert(knytos_tsd_key_delete(context_key) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    if (CASE != 60) puts("ERTS INIT normal explicit detached exits and TSD cleanup");
    puts("ERTS INIT PASS");
    return 0;
}
