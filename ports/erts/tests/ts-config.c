/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knytos_ethread.h"

#ifndef CASE
#define CASE 67
#endif

static atomic_uint attempts;
static atomic_uint live_allocations;
static atomic_uint fail_at;
static uint64_t cleanup_key;

struct allocation {
    max_align_t alignment;
    unsigned magic;
};

static void check_reentry(void)
{
    ethr_ts_event *unchanged = NULL;
    int on = 99;
    assert(ethr_make_ts_event__(&unchanged, 1) == EBUSY && unchanged == NULL);
    assert(knytos_ethr_ts_events_clear() == EBUSY);
    assert(ethr_get_main_thr_status(&on) == 0 && (on == 0 || on == 1));
    assert(ethr_set_main_thr_status(0, 0) == EBUSY);
}

static void *owned_alloc(size_t bytes)
{
    check_reentry();
    unsigned attempt = atomic_fetch_add(&attempts, 1) + 1;
    if (attempt == atomic_load(&fail_at)) return NULL;
    struct allocation *allocation = malloc(sizeof(*allocation) + bytes);
    assert(allocation);
    allocation->magic = 0x13572468;
    atomic_fetch_add(&live_allocations, 1);
    void *payload = allocation + 1;
    memset(payload, 0xa5, bytes);
    return payload;
}

static void *unused_realloc(void *pointer, size_t bytes)
{
    (void)pointer;
    (void)bytes;
    assert(0); /* The event owner uses fixed-size allocations. */
    return NULL;
}

static void owned_free(void *pointer)
{
    check_reentry();
    struct allocation *allocation = (struct allocation *)pointer - 1;
    assert(allocation->magic == 0x13572468);
    allocation->magic = 0;
    atomic_fetch_sub(&live_allocations, 1);
    free(allocation);
}

static void *trap_alloc(size_t bytes)
{
    (void)bytes;
    assert(0);
    return NULL;
}

static void trap_free(void *pointer)
{
    (void)pointer;
    assert(0);
}

static struct input {
    ethr_event gate;
    ethr_event ready;
    ethr_tid tid;
    unsigned index;
    unsigned cleaned;
} inputs[2];

static void cleanup(void *argument)
{
    struct input *input = argument;
    int on = 99;
    /* The earlier event key disposed the old classified context. */
    assert(ethr_get_main_thr_status(&on) == 0 && on == 0);
    ethr_ts_event *event = ethr_get_ts_event();
    assert(ethr_set_main_thr_status(1, 3) == 0);
    assert(ethr_equal_tids(ethr_self(), input->tid));
    ethr_leave_ts_event(event);
    ++input->cleaned;
}

static void *worker(void *argument)
{
    struct input *input = argument;
    int on = 99;
    assert(ethr_get_main_thr_status(&on) == 0 && on == 0);
    assert(ethr_set_main_thr_status(1, 2) == EINVAL);
    ethr_ts_event *event = ethr_get_ts_event();
    assert(ethr_set_main_thr_status(1, 2) == 0);
    assert(event->mtix == 2);
    assert(knytos_tsd_set(cleanup_key, input) == KNYTOS_OK);
    ethr_event_set(&input->ready);
    assert(ethr_event_wait(&input->gate) == 0);
    assert(ethr_get_main_thr_status(&on) == 0 && on == 1);
    ethr_leave_ts_event(event);
    if (input->index) ethr_thr_exit(input);
    return input;
}

int main(void)
{
    int on = 99;
    assert(ethr_get_main_thr_status(NULL) == EINVAL);
    assert(ethr_get_main_thr_status(&on) == 0 && on == 0);
    assert(ethr_set_main_thr_status(1, 1) == EINVAL);
    assert(ethr_no_used_tse() == 0);
    ethr_late_init_data data = ETHR_LATE_INIT_DATA_DEFAULT_INITER;
    data.main_threads = -1;
    assert(knytos_ethr_ts_events_init_opt(&data) == EINVAL);
    data.main_threads = KNYTOS_ETHR_MAIN_THREAD_LIMIT + 1;
    assert(knytos_ethr_ts_events_init_opt(&data) == ENOTSUP);
    data.main_threads = 3;
    data.reader_groups = -1;
    assert(knytos_ethr_ts_events_init_opt(&data) == EINVAL);
    data.reader_groups = 17; /* Advisory for the current native RW backend. */
    data.mem.std = (ethr_memory_allocator) {owned_alloc, unused_realloc, owned_free};
    if (CASE == 68)
        data.mem.std = (ethr_memory_allocator) {trap_alloc, NULL, trap_free};
    assert(knytos_ethr_ts_events_init_opt(&data) == 0);
    /* Mutating caller storage must not alter the published configuration. */
    data.mem.std = (ethr_memory_allocator) {trap_alloc, unused_realloc, trap_free};
    data.main_threads = 0;
    assert(knytos_ethr_ts_events_init_opt(&data) == EBUSY);
    ethr_ts_event *event = NULL;
    if (CASE == 67) {
        atomic_store(&fail_at, 1);
        assert(ethr_make_ts_event__(&event, 0) == ENOMEM && event == NULL);
        assert(atomic_load(&live_allocations) == 0 && ethr_no_used_tse() == 0);
        atomic_store(&fail_at, 3);
        assert(ethr_make_ts_event__(&event, 0) == ENOMEM && event == NULL);
        assert(atomic_load(&live_allocations) == 0 && ethr_no_used_tse() == 0);
        atomic_store(&fail_at, 0);
    }
    event = ethr_get_ts_event();
    assert(event->udata == NULL && event->mtix == 0);
    assert(ethr_set_main_thr_status(1, 3) == 0);
    assert(ethr_set_main_thr_status(1, 0) == EINVAL && event->mtix == 3);
    assert(ethr_set_main_thr_status(1, 4) == EINVAL && event->mtix == 3);
    ethr_ts_event *temporary = ethr_get_ts_event();
    assert(temporary != event && temporary->mtix == 3);
    assert(temporary->iflgs & ETHR_TS_EV_MAIN_THR);
    assert(ethr_set_main_thr_status(0, -1) == 0 && event->mtix == 0);
    assert(temporary->iflgs & ETHR_TS_EV_MAIN_THR);
    ethr_leave_ts_event(temporary);
    ethr_leave_ts_event(event);
    if (CASE == 67) {
        ethr_ts_event *unchanged = NULL;
        atomic_store(&fail_at, atomic_load(&attempts) + 1);
        assert(ethr_make_ts_event__(&unchanged, 1) == ENOMEM && unchanged == NULL);
        assert(atomic_load(&live_allocations) == 2 && ethr_no_used_tse() == 1);
        atomic_store(&fail_at, 0);
    }
    assert(knytos_ethr_ts_events_clear() == 0 && ethr_no_used_tse() == 0);
    assert(atomic_load(&live_allocations) == 0);
    if (CASE == 67) {
        assert(knytos_tsd_key_create_with_destructor(&cleanup_key, cleanup) == KNYTOS_OK);
        for (unsigned i = 0; i < 2; ++i) {
            inputs[i].index = i;
            assert(ethr_event_init(&inputs[i].gate) == 0);
            assert(ethr_event_init(&inputs[i].ready) == 0);
            assert(ethr_thr_create(&inputs[i].tid, worker, &inputs[i], NULL) == 0);
            assert(ethr_event_wait(&inputs[i].ready) == 0);
            assert(ethr_get_main_thr_status(&on) == 0 && on == 0);
            ethr_event_set(&inputs[i].gate);
            void *result = NULL;
            assert(ethr_thr_join(inputs[i].tid, &result) == 0 && result == &inputs[i]);
            assert(inputs[i].cleaned == 1 && atomic_load(&live_allocations) == 0);
            assert(ethr_no_used_tse() == 0);
            assert(ethr_event_destroy(&inputs[i].gate) == 0);
            assert(ethr_event_destroy(&inputs[i].ready) == 0);
        }
        assert(knytos_tsd_key_delete(cleanup_key) == KNYTOS_OK);
        puts("ERTS TS CONFIG captured allocator prefix failure and reentry rejection");
        puts("ERTS TS CONFIG participant status normal and explicit destructor cleanup");
    } else {
        assert(atomic_load(&attempts) == 0);
        puts("ERTS TS CONFIG incomplete allocator tuple uses whole fallback");
    }
    assert(knytos_ethr_threads_shutdown() == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS TS CONFIG PASS");
    return 0;
}
