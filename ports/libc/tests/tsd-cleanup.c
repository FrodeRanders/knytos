#include <assert.h>
#include <stdio.h>
#include "knytos.h"

#ifndef CASE
#define CASE 38
#endif

struct shared {
    uint64_t key;
    uint64_t deleted;
    uint64_t empty;
    struct knytos_event *event;
    unsigned calls;
};

static void unexpected(void *value)
{
    (void)value;
    assert(!"deleted or empty key destructor ran");
}

static void cleanup(void *value)
{
    struct shared *s = value;
    assert(!knytos_thread_is_startup());
    assert(knytos_tsd_get(s->key) == NULL);
    assert(knytos_event_wait_on(s->event, 0) == KNYTOS_OK);
    uint64_t temporary;
    assert(knytos_tsd_key_create(&temporary) == KNYTOS_OK);
    assert(knytos_tsd_key_delete(temporary) == KNYTOS_OK);
    s->calls++;
    if (CASE == 39 || s->calls % 4 != 0) {
        assert(knytos_tsd_set(s->key, s) == KNYTOS_OK);
    } else {
        assert(knytos_event_destroy(s->event) == KNYTOS_OK);
        s->event = NULL;
    }
}

static void worker(void *argument)
{
    struct shared *s = argument;
    assert(knytos_tsd_get(s->key) == NULL);
    s->event = knytos_event_create();
    assert(s->event);
    assert(knytos_event_set(s->event) == KNYTOS_OK);
    assert(knytos_tsd_set(s->key, s) == KNYTOS_OK);
    assert(knytos_tsd_set(s->deleted, s) == KNYTOS_OK);
    assert(knytos_tsd_key_delete(s->deleted) == KNYTOS_OK);
}

int main(void)
{
    struct shared s = {0};
    assert(knytos_tsd_key_create_with_destructor(&s.key, cleanup) == KNYTOS_OK);
    assert(knytos_tsd_key_create_with_destructor(&s.empty, unexpected) == KNYTOS_OK);
    assert(knytos_tsd_set(s.key, &s) == KNYTOS_OK);
    for (unsigned generation = 1; generation <= 2; ++generation) {
        assert(knytos_tsd_key_create_with_destructor(&s.deleted, unexpected) == KNYTOS_OK);
        assert(knytos_thread_start(1, 1, worker, &s) == KNYTOS_OK);
        assert(knytos_thread_join(1) == KNYTOS_OK);
        assert(s.calls == generation * 4 && s.event == NULL);
        assert(knytos_tsd_get(s.key) == &s);
    }
    assert(knytos_tsd_set(s.key, NULL) == KNYTOS_OK);
    assert(knytos_tsd_key_delete(s.key) == KNYTOS_OK);
    assert(knytos_tsd_key_delete(s.empty) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("TSD CLEANUP cleared values rearmed callbacks and event disposal");
    puts("TSD CLEANUP exact join and fresh generation");
    puts("TSD CLEANUP PASS");
    return 0;
}
