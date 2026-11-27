/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "knytos_ethread.h"

#ifndef CASE
#define CASE 64
#endif

static struct input {
    ethr_event gate;
    ethr_event ready;
    ethr_tid tid;
    ethr_ts_event *owned;
    unsigned index;
    unsigned handled;
    unsigned cleaned;
} inputs[4];
static uint64_t cleanup_key;
static ethr_ts_event *startup_event;

static void *entry(void *argument)
{
    struct input *input = argument;
    assert(knytos_ethr_ts_events_init() == EACCES);
    assert(ethr_free_ts_event__(startup_event) == EINVAL);
    ethr_ts_event *event = ethr_get_ts_event();
    input->owned = event;
    assert(event->iflgs == (ETHR_TS_EV_INITED | ETHR_TS_EV_ETHREAD | ETHR_TS_EV_BUSY));
    assert(ethr_atomic32_read(&event->uaflgs) == 0 && event->udata == NULL);
    assert(ethr_event_twait(&event->event, 0) == ETIMEDOUT);
    assert(knytos_tsd_set(cleanup_key, input) == KNYTOS_OK);
    ethr_event_set(&input->ready);
    assert(ethr_event_wait(&input->gate) == 0);
    if (CASE == 66) return input; /* Deliberately retain the busy borrow. */
    assert(ethr_equal_tids(ethr_self(), input->tid));
    assert(ethr_event_wait(&event->event) == 0);
    ethr_leave_ts_event(event);
    if (input->index == 1) ethr_thr_exit(input);
    return input;
}

static void handler(void)
{
    struct input *input = knytos_tsd_get(cleanup_key);
    ethr_ts_event *event = ethr_get_ts_event();
    assert(event == input->owned);
    assert(ethr_equal_tids(ethr_self(), input->tid));
    ethr_leave_ts_event(event);
    ++input->handled;
}

static void cleanup(void *argument)
{
    struct input *input = argument;
    assert(input->handled == 1);
    /* The earlier reserved key has already destroyed its old context. This
     * rearmed context must survive until the next native destructor pass. */
    ethr_ts_event *event = ethr_get_ts_event();
    assert(ethr_event_twait(&event->event, 0) == ETIMEDOUT);
    assert(ethr_atomic32_read(&event->uaflgs) == 0);
    assert(ethr_equal_tids(ethr_self(), input->tid));
    ethr_leave_ts_event(event);
    ++input->cleaned;
}

static void startup_lifetimes(void)
{
    ethr_ts_event *base = ethr_get_ts_event();
    startup_event = base;
    assert(ethr_no_used_tse() == 1);
    assert(ethr_free_ts_event__(base) == EBUSY);
    assert(knytos_ethr_ts_events_clear() == EBUSY);
    base->udata = &cleanup_key;
    base->uflgs = 17;
    base->rgix = 2;
    base->mtix = 3;
    ethr_atomic32_set(&base->uaflgs, 23);
    ethr_event_set(&base->event);
    ethr_ts_event *temporary = ethr_get_ts_event();
    assert(temporary != base && temporary->event.native != base->event.native);
    assert(temporary->udata == &cleanup_key && temporary->uflgs == 17);
    assert(temporary->rgix == 2 && temporary->mtix == 3);
    assert(ethr_atomic32_read(&temporary->uaflgs) == 23);
    assert(ethr_event_twait(&temporary->event, 0) == ETIMEDOUT);
    assert(ethr_no_used_tse() == 2);
    temporary->udata = NULL;
    ethr_leave_ts_event(temporary);
    base->udata = NULL;
    ethr_leave_ts_event(base);
    assert(ethr_no_used_tse() == 1);
    /* Clear may finish an idle head while retaining the exact busy tail. */
    assert(ethr_get_ts_event() == base);
    temporary = NULL;
    assert(ethr_make_ts_event__(&temporary, 1) == 0);
    assert(ethr_no_used_tse() == 2);
    assert(knytos_ethr_ts_events_clear() == EBUSY);
    assert(ethr_no_used_tse() == 1 && ethr_lookup_ts_event__(0) == base);
    ethr_leave_ts_event(base);
    /* Exercise both release orders for copied busy/peek metadata. */
    for (unsigned order = 0; order < 2; ++order) {
        assert(ethr_get_ts_event() == base);
        assert(ethr_peek_ts_event() == base);
        temporary = ethr_use_ts_event(base);
        assert(temporary != base);
        assert((temporary->iflgs & (ETHR_TS_EV_TMP | ETHR_TS_EV_BUSY | ETHR_TS_EV_PEEK))
            == (ETHR_TS_EV_TMP | ETHR_TS_EV_BUSY | ETHR_TS_EV_PEEK));
        if (order == 0) {
            ethr_leave_ts_event(temporary);
            assert(ethr_no_used_tse() == 2);
            ethr_unpeek_ts_event(temporary);
        } else {
            ethr_unpeek_ts_event(temporary);
            assert(ethr_no_used_tse() == 2);
            ethr_leave_ts_event(temporary);
        }
        ethr_unpeek_ts_event(base);
        ethr_leave_ts_event(base);
        assert(ethr_no_used_tse() == 1);
    }
    assert(ethr_get_ts_event() == base);
    ethr_ts_event *nested[KNYTOS_ETHR_TS_EVENT_LIMIT - 1];
    for (unsigned i = 0; i < KNYTOS_ETHR_TS_EVENT_LIMIT - 1; ++i)
        nested[i] = ethr_get_ts_event();
    ethr_ts_event *unchanged = base;
    assert(ethr_make_ts_event__(&unchanged, 0) == EAGAIN && unchanged == base);
    assert(ethr_no_used_tse() == KNYTOS_ETHR_TS_EVENT_LIMIT);
    for (unsigned i = 0; i < KNYTOS_ETHR_TS_EVENT_LIMIT - 1; ++i)
        ethr_leave_ts_event(nested[i]);
    ethr_leave_ts_event(base);
    base->udata = &cleanup_key;
    assert(knytos_ethr_ts_events_clear() == EBUSY);
    base->udata = NULL;
    ethr_event_reset(&base->event);
    puts("ERTS TSE independent temporary owners busy peek and capacity");
}

int main(void)
{
    ethr_ts_event *unchanged = NULL;
    assert(ethr_make_ts_event__(&unchanged, 0) == EACCES && unchanged == NULL);
    if (CASE == 65) {
        uint64_t keys[16];
        for (unsigned i = 0; i < 16; ++i)
            assert(knytos_tsd_key_create(&keys[i]) == KNYTOS_OK);
        assert(knytos_ethr_ts_events_init() == EAGAIN);
        assert(ethr_no_used_tse() == 0);
        assert(knytos_tsd_key_delete(keys[0]) == KNYTOS_OK);
        assert(knytos_ethr_ts_events_init() == 0);
        ethr_ts_event *event = ethr_get_ts_event();
        ethr_leave_ts_event(event);
        assert(knytos_ethr_ts_events_clear() == 0 && ethr_no_used_tse() == 0);
        for (unsigned i = 1; i < 16; ++i)
            assert(knytos_tsd_key_delete(keys[i]) == KNYTOS_OK);
        puts("ERTS TSE rejected key admission and fresh initialization");
    } else {
        assert(knytos_ethr_ts_events_init() == 0);
        assert(knytos_ethr_ts_events_init() == EBUSY);
        if (CASE == 64) startup_lifetimes();
        else startup_event = ethr_lookup_ts_event__(0);
        assert(knytos_tsd_key_create_with_destructor(&cleanup_key, cleanup) == KNYTOS_OK);
        if (CASE == 64) assert(ethr_install_exit_handler(handler) == 0);
        unsigned count = CASE == 64 ? 3 : 1;
        for (unsigned i = 0; i < count; ++i) {
            inputs[i].index = i;
            assert(ethr_event_init(&inputs[i].gate) == 0);
            assert(ethr_event_init(&inputs[i].ready) == 0);
            assert(ethr_thr_create(&inputs[i].tid, entry, &inputs[i], NULL) == 0);
            assert(ethr_event_wait(&inputs[i].ready) == 0);
        }
        assert(ethr_no_used_tse() == count + 1);
        for (unsigned i = 0; i < count; ++i) {
            ethr_event_set(&inputs[i].owned->event);
            ethr_event_set(&inputs[i].gate);
            void *result = NULL;
            assert(ethr_thr_join(inputs[i].tid, &result) == 0 && result == &inputs[i]);
            assert(inputs[i].handled == 1 && inputs[i].cleaned == 1);
            assert(ethr_event_destroy(&inputs[i].gate) == 0);
            assert(ethr_event_destroy(&inputs[i].ready) == 0);
        }
        assert(ethr_no_used_tse() == 1);
        inputs[3].index = 3;
        assert(ethr_event_init(&inputs[3].gate) == 0);
        assert(ethr_event_init(&inputs[3].ready) == 0);
        ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
        options.detached = 1;
        assert(ethr_thr_create(&inputs[3].tid, entry, &inputs[3], &options) == 0);
        assert(ethr_event_wait(&inputs[3].ready) == 0);
        ethr_event_set(&inputs[3].owned->event);
        ethr_event_set(&inputs[3].gate);
        assert(knytos_ethr_threads_shutdown() == 0);
        assert(inputs[3].handled == 1 && inputs[3].cleaned == 1);
        assert(ethr_event_destroy(&inputs[3].gate) == 0);
        assert(ethr_event_destroy(&inputs[3].ready) == 0);
        assert(ethr_no_used_tse() == 1);
        assert(knytos_ethr_ts_events_clear() == 0);
        assert(knytos_ethr_ts_events_clear() == 0 && ethr_no_used_tse() == 0);
        assert(knytos_tsd_key_delete(cleanup_key) == KNYTOS_OK);
        puts("ERTS TSE cross-CPU delivery destructor rearming and exact joins");
    }
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS TSE PASS");
    return 0;
}
