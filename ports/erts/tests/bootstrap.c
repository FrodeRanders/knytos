/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "knytos_ethread.h"

#ifndef CASE
#define CASE 80
#endif

static struct input {
    unsigned index;
    ethr_event gate;
    ethr_tid tid;
} inputs[3];
static unsigned prepared;
static atomic_uint children, parents, handlers;
static atomic_uint allocations, frees, live;
static unsigned fail_at = (unsigned)-1;
static int checking_late;

static void *entry(void *argument);

static void check_reentry(void)
{
    if (!checking_late) return;
    assert(knytos_thread_is_startup());
    ethr_tid unchanged = {99, 99};
    ethr_ts_event *event = NULL;
    assert(ethr_init(NULL) == EBUSY);
    assert(ethr_late_init(NULL) == EBUSY);
    assert(knytos_ethr_threads_init(NULL) == EBUSY);
    assert(knytos_ethr_threads_shutdown() == EBUSY);
    assert(ethr_thr_create(&unchanged, entry, NULL, NULL) == EBUSY);
    assert(unchanged.slot == 99 && unchanged.generation == 99);
    assert(ethr_make_ts_event__(&event, 0) == EBUSY && event == NULL);
    assert(knytos_ethr_ts_events_clear() == EBUSY);
    int on = 42;
    assert(ethr_get_main_thr_status(&on) == 0 && on == 0);
}

static void *allocate(size_t bytes)
{
    check_reentry();
    unsigned call = atomic_fetch_add(&allocations, 1);
    if (call == fail_at || (CASE == 82 && !knytos_thread_is_startup())) return NULL;
    void *pointer = malloc(bytes);
    if (pointer) atomic_fetch_add(&live, 1);
    return pointer;
}

static void release(void *pointer)
{
    check_reentry();
    assert(pointer);
    free(pointer);
    atomic_fetch_add(&frees, 1);
    assert(atomic_fetch_sub(&live, 1) != 0);
}

static void *resize(void *pointer, size_t bytes)
{
    (void)pointer;
    (void)bytes;
    abort(); /* The thread-event owner never reallocates published nodes. */
}

static void unexpected(void)
{
    static const char marker[] = "ERTS BOOTSTRAP unexpected cleanup or continuation\n";
    knytos_console_write(marker, sizeof(marker) - 1);
}

static void *prepare(void)
{
    assert(prepared < 3);
    return &inputs[prepared++];
}

static void child(void *argument)
{
    if (CASE == 82) unexpected();
    struct input *input = argument;
    assert(ethr_init(NULL) == EACCES && ethr_late_init(NULL) == EACCES);
    ethr_ts_event *event = ethr_peek_ts_event();
    assert((event->iflgs & (ETHR_TS_EV_ETHREAD | ETHR_TS_EV_INITED))
           == (ETHR_TS_EV_ETHREAD | ETHR_TS_EV_INITED));
    assert(!(event->iflgs & (ETHR_TS_EV_BUSY | ETHR_TS_EV_TMP)));
    input->tid = ethr_self();
    event->udata = input;
    ethr_unpeek_ts_event(event);
    if (CASE != 81) assert(ethr_set_main_thr_status(1, (int)input->index + 2) == 0);
    atomic_fetch_add(&children, 1);
}

static void parent(void *argument)
{
    if (CASE == 82) unexpected();
    struct input *input = argument;
    assert(atomic_load(&children) == input->index + 1);
    atomic_fetch_add(&parents, 1);
}

static void exit_handler(void)
{
    ethr_ts_event *event = ethr_get_ts_event();
    struct input *input = event->udata;
    assert(input && ethr_equal_tids(input->tid, ethr_self()));
    event->udata = NULL;
    ethr_leave_ts_event(event);
    atomic_fetch_add(&handlers, 1);
}

static void *entry(void *argument)
{
    if (CASE == 82) unexpected();
    struct input *input = argument;
    ethr_ts_event *event = ethr_peek_ts_event();
    assert(event->udata == input);
    ethr_unpeek_ts_event(event);
    assert(ethr_event_wait(&input->gate) == 0);
    if (input->index == 1) ethr_thr_exit(input);
    return input;
}

static void empty_heap(void)
{
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
}

int main(void)
{
    assert(ethr_late_init(NULL) == EACCES);
    ethr_init_data early = ETHR_INIT_DATA_DEFAULT_INITER;
    early.thread_create_prepare_func = prepare;
    early.thread_create_child_func = child;
    early.thread_create_parent_func = parent;
    if (CASE == 81) {
        uint64_t keys[16];
        for (unsigned i = 0; i < 15; ++i)
            assert(knytos_tsd_key_create(&keys[i]) == KNYTOS_OK);
        assert(ethr_init(&early) == EAGAIN);
        /* Identity-key preparation must have been cancelled after TSE-key
         * rejection: the last available registration can still be admitted. */
        assert(knytos_tsd_key_create(&keys[15]) == KNYTOS_OK);
        for (unsigned i = 0; i < 16; ++i)
            assert(knytos_tsd_key_delete(keys[i]) == KNYTOS_OK);
        empty_heap();
        puts("ERTS BOOTSTRAP key rejection rolls back before retry");
    }
    assert(ethr_init(&early) == 0);
    early = (ethr_init_data) ETHR_INIT_DATA_DEFAULT_INITER;
    assert(ethr_init(NULL) == EINVAL);
    assert(knytos_ethr_threads_init(NULL) == EBUSY);
    assert(knytos_ethr_ts_events_init() == EBUSY);
    ethr_tid unchanged = {99, 99};
    assert(ethr_thr_create(&unchanged, entry, NULL, NULL) == EACCES);
    assert(unchanged.slot == 99 && unchanged.generation == 99);
    ethr_ts_event *event = NULL;
    assert(ethr_make_ts_event__(&event, 0) == EACCES && event == NULL);
    assert(ethr_no_used_tse() == 0);
    empty_heap();

    ethr_late_init_data late = ETHR_LATE_INIT_DATA_DEFAULT_INITER;
    if (CASE != 81) {
        late.mem.std = (ethr_memory_allocator) {allocate, resize, release};
        late.main_threads = KNYTOS_ETHR_MAIN_THREAD_LIMIT + 1;
        assert(ethr_late_init(&late) == ENOTSUP);
        late.main_threads = KNYTOS_ETHR_MAIN_THREAD_LIMIT;
        late.reader_groups = -1;
        assert(ethr_late_init(&late) == EINVAL);
        late.reader_groups = 3;
        for (unsigned prefix = 0; prefix < 2; ++prefix) {
            fail_at = atomic_load(&allocations) + prefix;
            checking_late = 1;
            assert(ethr_late_init(&late) == ENOMEM);
            checking_late = 0;
            assert(atomic_load(&live) == 0 && ethr_no_used_tse() == 0);
            empty_heap();
        }
        fail_at = (unsigned)-1;
        checking_late = 1;
    }
    assert(ethr_late_init(CASE == 81 ? NULL : &late) == 0);
    checking_late = 0;
    late = (ethr_late_init_data) ETHR_LATE_INIT_DATA_DEFAULT_INITER;
    assert(ethr_late_init(NULL) == EINVAL);
    assert(ethr_no_used_tse() == 1);
    if (CASE != 81) {
        assert(atomic_load(&live) == 2);
        assert(ethr_set_main_thr_status(1, 1) == 0);
    }
    assert(ethr_install_exit_handler(exit_handler) == 0);
    if (CASE == 82) assert(atexit(unexpected) == 0);
    unsigned count = CASE == 80 ? 3 : 1;
    for (unsigned i = 0; i < count; ++i) {
        inputs[i].index = i;
        assert(ethr_event_init(&inputs[i].gate) == 0);
    }
    ethr_tid tids[3];
    for (unsigned i = 0; i < count; ++i) {
        ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
        options.detached = i == 2;
        assert(ethr_thr_create(&tids[i], entry, &inputs[i], &options) == 0);
        assert(ethr_equal_tids(tids[i], inputs[i].tid));
    }
    if (CASE == 82) unexpected();
    assert(ethr_no_used_tse() == count + 1);
    for (unsigned i = 0; i < count; ++i) ethr_event_set(&inputs[i].gate);
    for (unsigned i = 0; i < count && i < 2; ++i) {
        void *result = NULL;
        assert(ethr_thr_join(tids[i], &result) == 0 && result == &inputs[i]);
    }
    assert(knytos_ethr_threads_shutdown() == 0);
    assert(atomic_load(&children) == count && atomic_load(&parents) == count);
    assert(atomic_load(&handlers) == count && ethr_no_used_tse() == 1);
    assert(ethr_init(NULL) == ECANCELED && ethr_late_init(NULL) == ECANCELED);
    assert(knytos_ethr_ts_events_clear() == 0);
    assert(ethr_no_used_tse() == 0 && atomic_load(&live) == 0);
    for (unsigned i = 0; i < count; ++i) assert(ethr_event_destroy(&inputs[i].gate) == 0);
    empty_heap();
    puts("ERTS BOOTSTRAP late prefix rollback configuration and reentry fences");
    puts("ERTS BOOTSTRAP automatic events before child hooks and exit handlers");
    puts("ERTS BOOTSTRAP copied callbacks exact joins detached drain and zero owners");
    puts("ERTS BOOTSTRAP PASS");
    return 0;
}
