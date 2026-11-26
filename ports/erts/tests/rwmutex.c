/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"
#include "../ethr_rwmutex.c"

#ifndef CASE
#define CASE 55
#endif

struct input {
    ethr_rwmutex lock;
    ethr_event ready;
    ethr_event done;
    ethr_event release;
    unsigned payload;
};

static void *shared_reader(void *argument)
{
    struct input *input = argument;
    if (CASE == 56) ethr_rwmutex_runlock(&input->lock);
    ethr_rwmutex_rlock(&input->lock);
    assert(input->payload == 7);
    ethr_event_set(&input->ready);
    assert(ethr_event_wait(&input->release) == 0);
    ethr_rwmutex_runlock(&input->lock);
    return input;
}

static void *writer(void *argument)
{
    struct input *input = argument;
    assert(ethr_rwmutex_tryrwlock(&input->lock) == EBUSY);
    ethr_event_set(&input->ready);
    ethr_rwmutex_rwlock(&input->lock);
    assert(input->payload == 7);
    assert(ethr_rwmutex_tryrlock(&input->lock) == EBUSY);
    assert(ethr_rwmutex_tryrwlock(&input->lock) == EBUSY);
    input->payload = 19;
    ethr_rwmutex_rwunlock(&input->lock);
    ethr_event_set(&input->done);
    return input;
}

static void *blocked_reader(void *argument)
{
    struct input *input = argument;
    assert(ethr_rwmutex_tryrlock(&input->lock) == EBUSY);
    assert(ethr_rwmutex_tryrwlock(&input->lock) == EBUSY);
    ethr_event_set(&input->ready);
    ethr_rwmutex_rlock(&input->lock);
    assert(input->payload == 29);
    ethr_rwmutex_runlock(&input->lock);
    ethr_event_set(&input->done);
    return input;
}

static void join(ethr_tid tid, struct input *input)
{
    void *result = NULL;
    assert(ethr_thr_join(tid, &result) == 0 && result == input);
    ethr_event_reset(&input->ready);
    ethr_event_reset(&input->done);
}

int main(void)
{
    struct input input = {.payload = 7};
    ethr_rwmutex_opt options = ETHR_RWMUTEX_OPT_DEFAULT_INITER;
    options.main_spincount = -2;
    assert(ethr_rwmutex_init_opt(&input.lock, &options) == EINVAL);
    assert(input.lock.native == NULL);
    options.main_spincount = 100;
    options.aux_spincount = 0;
    options.type = (ethr_rwmutex_type)99;
    assert(ethr_rwmutex_init_opt(&input.lock, &options) == EINVAL);
    options.type = ETHR_RWMUTEX_TYPE_FREQUENT_READ;
    options.lived = ETHR_RWMUTEX_LONG_LIVED;
    struct knytos_allocation_stats before, after;
    assert(knytos_allocation_stats(&before) == KNYTOS_OK);
    assert(ethr_rwmutex_init_opt(&input.lock, &options) == 0);
    assert(knytos_allocation_stats(&after) == KNYTOS_OK);
    assert(ethr_rwmutex_size(&input.lock) > 0);
    assert(after.requested_bytes - before.requested_bytes == ethr_rwmutex_size(&input.lock));
    assert(ethr_rwmutex_set_reader_group(0) == 0);
    assert(ethr_rwmutex_set_reader_group(17) == 0);
    assert(ethr_event_init(&input.ready) == 0);
    assert(ethr_event_init(&input.done) == 0);
    assert(ethr_event_init(&input.release) == 0);
    assert(ethr_rwmutex_tryrlock(&input.lock) == 0);
    ethr_rwmutex_rlock(&input.lock);
    assert(ethr_rwmutex_tryrwlock(&input.lock) == EBUSY);
    ethr_tid tid;
    assert(ethr_thr_create(&tid, shared_reader, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    assert(ethr_rwmutex_destroy(&input.lock) == EBUSY);
    ethr_event_set(&input.release);
    join(tid, &input);
    assert(ethr_thr_create(&tid, writer, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    assert(ethr_event_twait(&input.done, 10000000) == ETIMEDOUT);
    ethr_rwmutex_runlock(&input.lock);
    assert(ethr_event_twait(&input.done, 10000000) == ETIMEDOUT);
    ethr_rwmutex_runlock(&input.lock);
    assert(ethr_event_wait(&input.done) == 0);
    join(tid, &input);
    assert(ethr_rwmutex_tryrwlock(&input.lock) == 0);
    assert(input.payload == 19);
    assert(ethr_thr_create(&tid, blocked_reader, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    assert(ethr_event_twait(&input.done, 10000000) == ETIMEDOUT);
    assert(ethr_rwmutex_destroy(&input.lock) == EBUSY);
    input.payload = 29;
    ethr_rwmutex_rwunlock(&input.lock);
    assert(ethr_event_wait(&input.done) == 0);
    join(tid, &input);
    assert(ethr_rwmutex_destroy(&input.lock) == 0);
    assert(ethr_rwmutex_destroy(&input.lock) == EINVAL);
    assert(ethr_rwmutex_tryrlock(&input.lock) == EINVAL);
    assert(ethr_rwmutex_tryrwlock(&input.lock) == EINVAL);
    assert(ethr_rwmutex_size(&input.lock) == 0);
    assert(ethr_rwmutex_init(&input.lock) == 0);
    assert(ethr_rwmutex_destroy(&input.lock) == 0);
    options.type = ETHR_RWMUTEX_TYPE_EXTREMELY_FREQUENT_READ;
    options.lived = ETHR_RWMUTEX_SHORT_LIVED;
    assert(ethr_rwmutex_init_opt(&input.lock, &options) == 0);
    assert(ethr_rwmutex_destroy(&input.lock) == 0);
    assert(ethr_event_destroy(&input.ready) == 0);
    assert(ethr_event_destroy(&input.done) == 0);
    assert(ethr_event_destroy(&input.release) == 0);
    assert(knytos_allocation_stats(&after) == KNYTOS_OK && after.blocks == 0);
    puts("ERTS RWMUTEX shared readers and last-reader writer release");
    puts("ERTS RWMUTEX event-only reader wake and publication");
    puts("ERTS RWMUTEX busy destruction and heap size");
    puts("ERTS RWMUTEX PASS");
    return 0;
}
