/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"

struct input {
    ethr_event gate;
    ethr_tid expected;
    uint64_t cleanup_key;
    unsigned cleaned;
};

static void cleanup_identity(void *value)
{
    struct input *input = value;
    assert(ethr_equal_tids(ethr_self(), input->expected));
    assert(knytos_tsd_get(input->cleanup_key) == NULL);
    input->cleaned++;
}

static void *child(void *argument)
{
    struct input *input = argument;
    assert(ethr_event_wait(&input->gate) == 0);
    assert(ethr_equal_tids(ethr_self(), input->expected));
    ethr_tid rejected = {99, 99};
    assert(ethr_thr_create(&rejected, child, argument, NULL) == EACCES);
    assert(rejected.slot == 99 && rejected.generation == 99);
    assert(ethr_thr_join(input->expected, NULL) == EACCES);
    assert(knytos_tsd_set(input->cleanup_key, input) == KNYTOS_OK);
    return argument;
}

int main(void)
{
    ethr_tid main_id = ethr_self();
    assert(ethr_equal_tids(main_id, ethr_self()));
    ethr_tid unchanged = {99, 99};
    ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
    options.suggested_stack_size = 16;
    assert(ethr_thr_create(&unchanged, child, NULL, &options) == ENOTSUP);
    options.suggested_stack_size = -1;
    options.name = "sixteen-letters!";
    assert(ethr_thr_create(&unchanged, child, NULL, &options) == EINVAL);
    assert(unchanged.slot == 99 && unchanged.generation == 99);
    assert(ethr_thr_create(NULL, child, NULL, NULL) == EINVAL);
    assert(ethr_thr_detach(main_id) == EINVAL);

    struct input inputs[3] = {0};
    for (unsigned i = 0; i < 3; ++i)
        assert(ethr_event_init(&inputs[i].gate) == 0);
    /* Allocate the ordinary identity key before the destructor key. Exit
     * cleanup must keep earlier ordinary keys available to later callbacks. */
    assert(ethr_thr_create(&inputs[0].expected, child, &inputs[0], NULL) == 0);
    uint64_t key;
    assert(knytos_tsd_key_create_with_destructor(&key, cleanup_identity) == KNYTOS_OK);
    for (unsigned i = 0; i < 3; ++i) {
        inputs[i].cleanup_key = key;
        if (i != 0)
            assert(ethr_thr_create(&inputs[i].expected, child, &inputs[i], NULL) == 0);
        assert(!ethr_equal_tids(main_id, inputs[i].expected));
    }
    assert(ethr_thr_create(&unchanged, child, NULL, NULL) == EAGAIN);
    assert(unchanged.slot == 99 && unchanged.generation == 99);
    for (unsigned i = 0; i < 3; ++i) {
        ethr_event_set(&inputs[i].gate);
        void *result = NULL;
        assert(ethr_thr_join(inputs[i].expected, &result) == 0);
        assert(result == &inputs[i] && inputs[i].cleaned == 1);
    }
    puts("ERTS THREAD bounded roster results and destructor identity");
    ethr_tid old = inputs[0].expected;
    ethr_event_reset(&inputs[0].gate);
    assert(ethr_thr_create(&inputs[0].expected, child, &inputs[0], NULL) == 0);
    assert(!ethr_equal_tids(old, inputs[0].expected));
    void *untouched = &old;
    assert(ethr_thr_join(old, &untouched) == EINVAL && untouched == &old);
    ethr_event_set(&inputs[0].gate);
    assert(ethr_thr_join(inputs[0].expected, NULL) == 0);
    assert(inputs[0].cleaned == 2);
    for (unsigned i = 0; i < 3; ++i)
        assert(ethr_event_destroy(&inputs[i].gate) == 0);
    assert(knytos_tsd_key_delete(key) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS THREAD stale identity and slot reuse");
    puts("ERTS THREAD PASS");
    return 0;
}
