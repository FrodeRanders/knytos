/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "knytos_ethread.h"

/* These objects are identity markers for the metadata API, not claimed
 * stack boundaries. No test dereferences an alleged native stack limit. */
static unsigned char startup_marker;
static unsigned char replacement_marker;
static struct input {
    ethr_event gate;
    ethr_tid tid;
    unsigned char marker;
    unsigned handlers;
    unsigned cleaned;
    unsigned index;
} inputs[4];
static unsigned next_child;
static uint64_t context_key;

static void child_init(void *argument)
{
    assert(argument == NULL);
    struct input *input = &inputs[next_child++];
    assert(ethr_get_stacklimit() == NULL);
    assert(ethr_set_stacklimit(NULL) == EINVAL);
    assert(ethr_get_stacklimit() == NULL);
    assert(ethr_set_stacklimit(&input->marker) == 0);
    assert(ethr_get_stacklimit() == &input->marker);
    assert(ethr_set_stacklimit(&replacement_marker) == EACCES);
    assert(ethr_set_stacklimit(NULL) == EACCES);
    assert(ethr_get_stacklimit() == &input->marker);
    assert(knytos_tsd_set(context_key, input) == KNYTOS_OK);
}

static void exit_handler(void)
{
    struct input *input = knytos_tsd_get(context_key);
    assert(input && ethr_get_stacklimit() == &input->marker);
    assert(ethr_equal_tids(ethr_self(), input->tid));
    ++input->handlers;
}

static void cleanup(void *argument)
{
    struct input *input = argument;
    assert(input->handlers == 1);
    assert(ethr_get_stacklimit() == &input->marker);
    assert(ethr_set_stacklimit(NULL) == EACCES);
    assert(ethr_equal_tids(ethr_self(), input->tid));
    ++input->cleaned;
}

static void *entry(void *argument)
{
    struct input *input = argument;
    assert(knytos_tsd_get(context_key) == input);
    assert(ethr_get_stacklimit() == &input->marker);
    assert(ethr_event_wait(&input->gate) == 0);
    if (input->index == 1) ethr_thr_exit(input);
    return input;
}

int main(void)
{
    assert(ethr_get_stacklimit() == NULL);
    assert(ethr_set_stacklimit(NULL) == EINVAL);
    assert(ethr_set_stacklimit(&startup_marker) == 0);
    assert(ethr_set_stacklimit(&replacement_marker) == EACCES);
    assert(ethr_set_stacklimit(NULL) == EACCES);
    ethr_init_data data = ETHR_INIT_DATA_DEFAULT_INITER;
    data.thread_create_child_func = child_init;
    assert(knytos_ethr_threads_init(&data) == 0);
    assert(ethr_install_exit_handler(exit_handler) == 0);
    assert(knytos_tsd_key_create_with_destructor(&context_key, cleanup) == KNYTOS_OK);
    for (unsigned i = 0; i < 4; ++i) {
        inputs[i].index = i;
        assert(ethr_event_init(&inputs[i].gate) == 0);
    }
    /* Keep three distinct participants live while observing startup's value. */
    for (unsigned i = 0; i < 3; ++i) {
        assert(ethr_thr_create(&inputs[i].tid, entry, &inputs[i], NULL) == 0);
        assert(ethr_get_stacklimit() == &startup_marker);
    }
    for (unsigned i = 0; i < 3; ++i) {
        ethr_event_set(&inputs[i].gate);
        void *result = NULL;
        assert(ethr_thr_join(inputs[i].tid, &result) == 0 && result == &inputs[i]);
        assert(inputs[i].handlers == 1 && inputs[i].cleaned == 1);
    }
    ethr_tid retired = inputs[0].tid;
    ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
    options.detached = 1;
    assert(ethr_thr_create(&inputs[3].tid, entry, &inputs[3], &options) == 0);
    assert(inputs[3].tid.slot == retired.slot);
    assert(inputs[3].tid.generation != retired.generation);
    ethr_event_set(&inputs[3].gate);
    assert(knytos_ethr_threads_shutdown() == 0);
    assert(inputs[3].handlers == 1 && inputs[3].cleaned == 1);
    assert(next_child == 4 && ethr_get_stacklimit() == &startup_marker);
    for (unsigned i = 0; i < 4; ++i)
        assert(ethr_event_destroy(&inputs[i].gate) == 0);
    assert(knytos_tsd_key_delete(context_key) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS STACKLIMIT unset null rejection and one-time installation");
    puts("ERTS STACKLIMIT isolated participants and fresh slot generation");
    puts("ERTS STACKLIMIT handlers destructors explicit exit and detached drain");
    puts("ERTS STACKLIMIT PASS");
    return 0;
}
