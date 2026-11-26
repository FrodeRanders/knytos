/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"

struct input {
    ethr_event gate;
    ethr_event cleanup_done;
    ethr_tid tid;
    uint64_t cleanup_key;
    unsigned cleaned;
};

static void cleanup_detached(void *argument)
{
    struct input *input = argument;
    assert(ethr_equal_tids(ethr_self(), input->tid));
    input->cleaned++;
    ethr_event_set(&input->cleanup_done);
}

static void *detached_entry(void *argument)
{
    struct input *input = argument;
    assert(ethr_event_wait(&input->gate) == 0);
    assert(knytos_ethr_threads_shutdown() == EACCES);
    assert(ethr_thr_detach(input->tid) == EACCES);
    assert(knytos_tsd_set(input->cleanup_key, input) == KNYTOS_OK);
    return argument;
}

int main(void)
{
    uint64_t key;
    assert(knytos_tsd_key_create_with_destructor(&key, cleanup_detached) == KNYTOS_OK);
    struct input inputs[3] = {0};
    ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
    for (unsigned i = 0; i < 3; ++i) {
        inputs[i].cleanup_key = key;
        assert(ethr_event_init(&inputs[i].gate) == 0);
        assert(ethr_event_init(&inputs[i].cleanup_done) == 0);
        options.detached = i != 1;
        assert(ethr_thr_create(&inputs[i].tid, detached_entry, &inputs[i], &options) == 0);
    }
    assert(ethr_thr_detach(inputs[1].tid) == 0);
    for (unsigned i = 0; i < 3; ++i) {
        void *unchanged = &key;
        assert(ethr_thr_join(inputs[i].tid, &unchanged) == EINVAL && unchanged == &key);
        assert(ethr_thr_detach(inputs[i].tid) == EINVAL);
        assert(inputs[i].cleaned == 0);
    }
    ethr_tid rejected = {99, 99};
    assert(ethr_thr_create(&rejected, detached_entry, NULL, NULL) == EAGAIN);
    puts("ERTS DETACHED retained admission and join rejection");
    for (unsigned i = 0; i < 3; ++i)
        ethr_event_set(&inputs[i].gate);
    for (unsigned i = 0; i < 3; ++i)
        assert(ethr_event_wait(&inputs[i].cleanup_done) == 0);
    assert(ethr_thr_create(&rejected, detached_entry, NULL, NULL) == EAGAIN);
    assert(knytos_ethr_threads_shutdown() == 0);
    for (unsigned i = 0; i < 3; ++i) {
        assert(inputs[i].cleaned == 1);
        assert(ethr_thr_detach(inputs[i].tid) == EINVAL);
        assert(ethr_event_destroy(&inputs[i].gate) == 0);
        assert(ethr_event_destroy(&inputs[i].cleanup_done) == 0);
    }
    assert(ethr_thr_create(&rejected, detached_entry, NULL, NULL) == ECANCELED);
    assert(rejected.slot == 99 && rejected.generation == 99);
    assert(knytos_ethr_threads_shutdown() == 0);
    assert(knytos_tsd_key_delete(key) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS DETACHED shutdown joined destructors and fenced creation");
    puts("ERTS DETACHED PASS");
    return 0;
}
