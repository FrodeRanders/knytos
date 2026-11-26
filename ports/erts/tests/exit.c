/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"

#ifndef CASE
#define CASE 46
#endif

struct input {
    ethr_tid tid;
    ethr_event gate;
    ethr_event owned;
    uint64_t key;
    unsigned cleaned;
    void *result;
};

static void exit_cleanup(void *argument)
{
    struct input *input = argument;
    assert(ethr_equal_tids(ethr_self(), input->tid));
    assert(knytos_tsd_get(input->key) == NULL);
    if (CASE == 47) ethr_thr_exit(input);
    assert(ethr_event_destroy(&input->owned) == 0);
    input->cleaned++;
}

static void exit_nested(struct input *input)
{
    ethr_thr_exit(input->result);
}

static void *exiting_entry(void *argument)
{
    struct input *input = argument;
    assert(ethr_event_wait(&input->gate) == 0);
    assert(knytos_tsd_set(input->key, input) == KNYTOS_OK);
    exit_nested(input);
    assert(!"explicit exit returned to entry");
    return NULL;
}

int main(void)
{
    struct input input = {0};
    assert(ethr_event_init(&input.gate) == 0);
    assert(knytos_tsd_key_create_with_destructor(&input.key, exit_cleanup) == KNYTOS_OK);
    ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
    options.name = "explicit-exit";
    for (unsigned generation = 0; generation < 2; ++generation) {
        assert(ethr_event_init(&input.owned) == 0);
        input.result = generation ? NULL : &input.cleaned;
        assert(ethr_thr_create(&input.tid, exiting_entry, &input, &options) == 0);
        ethr_event_set(&input.gate);
        void *result = &input;
        assert(ethr_thr_join(input.tid, &result) == 0);
        assert(result == input.result && input.cleaned == generation + 1);
        assert(input.owned.native == NULL);
        ethr_event_reset(&input.gate);
    }
    puts("ERTS EXIT nested exit results cleanup and slot reuse");

    options.detached = 1;
    assert(ethr_event_init(&input.owned) == 0);
    assert(ethr_thr_create(&input.tid, exiting_entry, &input, &options) == 0);
    ethr_event_set(&input.gate);
    assert(ethr_thr_join(input.tid, NULL) == EINVAL);
    assert(knytos_ethr_threads_shutdown() == 0);
    assert(input.cleaned == 3 && input.owned.native == NULL);
    assert(ethr_event_destroy(&input.gate) == 0);
    assert(knytos_tsd_key_delete(input.key) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS EXIT detached exit settled by shutdown drain");
    puts("ERTS EXIT PASS");
    return 0;
}
