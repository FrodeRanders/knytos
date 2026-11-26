/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "knytos_ethread.h"

#ifndef CASE
#define CASE 57
#endif

struct input {
    ethr_spinlock_t spin;
    ethr_rwlock_t rw;
    ethr_event ready;
    ethr_event done;
    ethr_event release;
    ethr_tsd_key key;
    unsigned payload;
};

static void *spin_worker(void *argument)
{
    struct input *input = argument;
    assert(ethr_tsd_get(input->key) == NULL);
    assert(ethr_tsd_set(input->key, input) == 0);
    if (CASE == 58) ethr_spin_unlock(&input->spin);
    ethr_event_set(&input->ready);
    ethr_spin_lock(&input->spin);
    assert(input->payload == 41);
    input->payload = 42;
    ethr_spin_unlock(&input->spin);
    ethr_event_set(&input->done);
    return input;
}

static void *shared_reader(void *argument)
{
    struct input *input = argument;
    ethr_read_lock(&input->rw);
    assert(input->payload == 42);
    ethr_event_set(&input->ready);
    assert(ethr_event_wait(&input->release) == 0);
    ethr_read_unlock(&input->rw);
    return input;
}

static void *writer(void *argument)
{
    struct input *input = argument;
    ethr_event_set(&input->ready);
    ethr_write_lock(&input->rw);
    assert(input->payload == 42);
    input->payload = 43;
    ethr_write_unlock(&input->rw);
    ethr_event_set(&input->done);
    return input;
}

static void *blocked_reader(void *argument)
{
    struct input *input = argument;
    ethr_event_set(&input->ready);
    ethr_read_lock(&input->rw);
    assert(input->payload == 44);
    ethr_read_unlock(&input->rw);
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
    struct input input = {.payload = 41};
    assert(ethr_spinlock_init(&input.spin) == 0);
    assert(ethr_rwlock_init(&input.rw) == 0);
    assert(ethr_event_init(&input.ready) == 0);
    assert(ethr_event_init(&input.done) == 0);
    assert(ethr_event_init(&input.release) == 0);
    assert(ethr_tsd_key_create(&input.key, NULL) == 0);
    assert(ethr_tsd_set(input.key, &input) == 0);
    ethr_spin_lock(&input.spin);
    ethr_tid tid;
    assert(ethr_thr_create(&tid, spin_worker, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    assert(ethr_event_twait(&input.done, 10000000) == ETIMEDOUT);
    assert(ethr_spinlock_destroy(&input.spin) == EBUSY);
    ethr_spin_unlock(&input.spin);
    assert(ethr_event_wait(&input.done) == 0);
    join(tid, &input);
    ethr_spin_lock(&input.spin);
    assert(input.payload == 42);
    ethr_compiler_barrier();
    ethr_spin_unlock(&input.spin);
    assert(ethr_spinlock_destroy(&input.spin) == 0);
    assert(ethr_spinlock_destroy(&input.spin) == EINVAL);
    ethr_read_lock(&input.rw);
    assert(ethr_thr_create(&tid, shared_reader, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    assert(ethr_rwlock_destroy(&input.rw) == EBUSY);
    ethr_event_set(&input.release);
    join(tid, &input);
    assert(ethr_thr_create(&tid, writer, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    assert(ethr_event_twait(&input.done, 10000000) == ETIMEDOUT);
    ethr_read_unlock(&input.rw);
    assert(ethr_event_wait(&input.done) == 0);
    join(tid, &input);
    ethr_write_lock(&input.rw);
    assert(input.payload == 43);
    assert(ethr_thr_create(&tid, blocked_reader, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    assert(ethr_event_twait(&input.done, 10000000) == ETIMEDOUT);
    input.payload = 44;
    ethr_write_unlock(&input.rw);
    assert(ethr_event_wait(&input.done) == 0);
    join(tid, &input);
    assert(ethr_rwlock_destroy(&input.rw) == 0);
    assert(ethr_rwlock_destroy(&input.rw) == EINVAL);
    assert(ethr_tsd_get(input.key) == &input);
    assert(ethr_tsd_key_delete(input.key) == 0);
    assert(ethr_event_destroy(&input.ready) == 0);
    assert(ethr_event_destroy(&input.done) == 0);
    assert(ethr_event_destroy(&input.release) == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS LOCKS archive-linked blocking spin fallback");
    puts("ERTS LOCKS shared readers writer wake and reader publication");
    puts("ERTS LOCKS public headers TSD and exact joins");
    puts("ERTS LOCKS PASS");
    return 0;
}
