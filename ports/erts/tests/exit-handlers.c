/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"

#ifndef CASE
#define CASE 48
#endif

static uint64_t context_key;
struct input {
    ethr_tid tid;
    ethr_event gate;
    ethr_event owned;
    unsigned stage;
    unsigned cleaned;
    unsigned mode;
};

static void handler_first(void)
{
    struct input *input = knytos_tsd_get(context_key);
    assert(input && input->stage == 1);
    assert(ethr_equal_tids(ethr_self(), input->tid));
    input->stage = 2;
}

static void handler_second(void)
{
    struct input *input = knytos_tsd_get(context_key);
    assert(input && input->stage == 0);
    assert(ethr_equal_tids(ethr_self(), input->tid));
    assert(ethr_install_exit_handler(handler_first) == EACCES);
    if (CASE == 49) ethr_thr_exit(input);
    assert(ethr_event_twait(&input->owned, 0) == ETIMEDOUT);
    ethr_event_set(&input->owned);
    input->stage = 1;
}

static void cleanup(void *argument)
{
    struct input *input = argument;
    assert(input->stage == 2);
    assert(knytos_tsd_get(context_key) == NULL);
    assert(ethr_event_wait(&input->owned) == 0);
    assert(ethr_event_destroy(&input->owned) == 0);
    input->cleaned++;
}

static void *entry(void *argument)
{
    struct input *input = argument;
    assert(ethr_event_wait(&input->gate) == 0);
    assert(knytos_tsd_set(context_key, input) == KNYTOS_OK);
    if (input->mode) ethr_thr_exit(input);
    return input;
}

static void never_called(void)
{
    assert(!"capacity fixture handler was called");
}

int main(void)
{
    if (CASE == 50) {
        for (unsigned i = 0; i < 16; ++i)
            assert(ethr_install_exit_handler(never_called) == 0);
        assert(ethr_install_exit_handler(never_called) == EAGAIN);
        puts("ERTS HANDLERS bounded registration PASS");
        return 0;
    }
    assert(ethr_install_exit_handler(NULL) == EINVAL);
    assert(ethr_install_exit_handler(handler_first) == 0);
    assert(ethr_install_exit_handler(handler_second) == 0);
    assert(knytos_tsd_key_create_with_destructor(&context_key, cleanup) == KNYTOS_OK);
    struct input input = {0};
    assert(ethr_event_init(&input.gate) == 0);
    ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
    options.name = "exit-handlers";
    for (unsigned i = 0; i < 3; ++i) {
        input.stage = 0;
        input.mode = i != 0;
        options.detached = i == 2;
        assert(ethr_event_init(&input.owned) == 0);
        assert(ethr_thr_create(&input.tid, entry, &input, &options) == 0);
        ethr_event_set(&input.gate);
        if (options.detached) {
            assert(knytos_ethr_threads_shutdown() == 0);
        } else {
            void *result = NULL;
            assert(ethr_thr_join(input.tid, &result) == 0 && result == &input);
        }
        assert(input.stage == 2 && input.cleaned == i + 1);
        ethr_event_reset(&input.gate);
    }
    assert(ethr_install_exit_handler(never_called) == ECANCELED);
    assert(ethr_event_destroy(&input.gate) == 0);
    assert(knytos_tsd_key_delete(context_key) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS HANDLERS reverse order before TSD and exact joins");
    puts("ERTS HANDLERS normal explicit and detached exits");
    puts("ERTS HANDLERS PASS");
    return 0;
}
