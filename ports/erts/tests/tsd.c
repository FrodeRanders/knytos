/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

/* Build the adapter components into one standalone qualification image. */
#include "../ethr_tsd.c"
#include "../ethr_event.c"

struct shared {
    ethr_tsd_key data_key;
    ethr_tsd_key event_key;
    unsigned cleaned;
};

struct thread_state {
    ethr_event event;
    struct shared *shared;
};

static void destroy_thread_state(void *value)
{
    struct thread_state *state = value;
    struct shared *s = state->shared;
    assert(!knytos_thread_is_startup());
    assert(ethr_tsd_get(s->event_key) == NULL);
    assert(ethr_event_wait(&state->event) == 0);
    assert(ethr_event_destroy(&state->event) == 0);
    s->cleaned++;
    free(state);
}

static void worker(void *argument)
{
    struct shared *s = argument;
    assert(ethr_tsd_get(s->data_key) == NULL);
    assert(ethr_tsd_get(s->event_key) == NULL);
    assert(ethr_tsd_set(s->data_key, &s->cleaned) == 0);
    assert(ethr_tsd_get(s->data_key) == &s->cleaned);
    struct thread_state *state = malloc(sizeof(*state));
    assert(state);
    state->shared = s;
    assert(ethr_event_init(&state->event) == 0);
    ethr_event_set(&state->event);
    assert(ethr_tsd_set(s->event_key, state) == 0);
}

int main(void)
{
    assert(ethr_tsd_key_create(NULL, NULL) == EINVAL);
    assert(ethr_tsd_key_delete(0) == EINVAL);
    assert(ethr_tsd_set(0, NULL) == EINVAL);
    assert(ethr_tsd_get(0) == NULL);
    ethr_tsd_key keys[16];
    for (unsigned i = 0; i < 16; ++i)
        assert(ethr_tsd_key_create(&keys[i], "bounded") == 0);
    ethr_tsd_key rejected = UINT64_MAX;
    assert(ethr_tsd_key_create(&rejected, NULL) == EAGAIN);
    assert(rejected == UINT64_MAX);
    for (unsigned i = 0; i < 16; ++i)
        assert(ethr_tsd_key_delete(keys[i]) == 0);
    ethr_tsd_key replacement;
    assert(ethr_tsd_key_create(&replacement, NULL) == 0);
    assert(replacement != keys[0]);
    assert(ethr_tsd_set(keys[0], &replacement) == EINVAL);
    assert(ethr_tsd_key_delete(keys[0]) == EINVAL);
    assert(ethr_tsd_get(keys[0]) == NULL);
    assert(ethr_tsd_key_delete(replacement) == 0);
    puts("ERTS TSD key exhaustion and stale errno results");

    struct shared s = {0};
    assert(ethr_tsd_key_create(&s.data_key, "data") == 0);
    /* The enclosing backend registers its internal cleanup callback. The
     * public ethr_tsd_key_create entry point retains its no-destructor API. */
    assert(knytos_tsd_key_create_with_destructor(&s.event_key, destroy_thread_state) == KNYTOS_OK);
    assert(ethr_tsd_set(s.data_key, &s) == 0);
    for (unsigned generation = 1; generation <= 2; ++generation) {
        assert(knytos_thread_start(1, 1, worker, &s) == KNYTOS_OK);
        assert(knytos_thread_join(1) == KNYTOS_OK);
        assert(s.cleaned == generation);
        assert(ethr_tsd_get(s.data_key) == &s);
    }
    assert(ethr_tsd_set(s.data_key, NULL) == 0);
    assert(ethr_tsd_key_delete(s.data_key) == 0);
    assert(ethr_tsd_key_delete(s.event_key) == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS TSD isolated values and event destruction before join");
    puts("ERTS TSD PASS");
    return 0;
}
