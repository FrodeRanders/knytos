/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"
#include "../ethr_mutex.c"

#ifndef CASE
#define CASE 51
#endif

struct input {
    ethr_mutex mutex;
    ethr_event started;
    ethr_event done;
    unsigned payload;
};

static void *contended(void *argument)
{
    struct input *input = argument;
    if (CASE == 52) ethr_mutex_unlock(&input->mutex);
    assert(ethr_mutex_trylock(&input->mutex) == EBUSY);
    ethr_event_set(&input->started);
    ethr_mutex_lock(&input->mutex);
    assert(input->payload == 41);
    input->payload = 42;
    ethr_mutex_unlock(&input->mutex);
    ethr_event_set(&input->done);
    return input;
}

int main(void)
{
    struct input input = {0};
    ethr_mutex_opt options = ETHR_MUTEX_OPT_DEFAULT_INITER;
    options.main_spincount = -2;
    assert(ethr_mutex_init_opt(&input.mutex, &options) == EINVAL);
    assert(input.mutex.native == NULL);
    options.main_spincount = 100;
    options.aux_spincount = 0;
    options.posix_compliant = 1;
    assert(ethr_mutex_init_opt(&input.mutex, &options) == 0);
    assert(ethr_event_init(&input.started) == 0);
    assert(ethr_event_init(&input.done) == 0);
    assert(ethr_mutex_trylock(&input.mutex) == 0);
    assert(ethr_mutex_trylock(&input.mutex) == EBUSY);
    input.payload = 41;
    ethr_tid tid;
    assert(ethr_thr_create(&tid, contended, &input, NULL) == 0);
    assert(ethr_event_wait(&input.started) == 0);
    /* A bounded negative observation while startup retains the mutex. */
    assert(ethr_event_twait(&input.done, 10000000) == ETIMEDOUT);
    ethr_mutex_unlock(&input.mutex);
    assert(ethr_event_wait(&input.done) == 0);
    void *result = NULL;
    assert(ethr_thr_join(tid, &result) == 0 && result == &input);
    ethr_mutex_lock(&input.mutex);
    assert(input.payload == 42);
    assert(ethr_mutex_destroy(&input.mutex) == EBUSY);
    ethr_mutex_unlock(&input.mutex);
    assert(ethr_mutex_destroy(&input.mutex) == 0);
    assert(ethr_mutex_destroy(&input.mutex) == EINVAL);
    assert(ethr_mutex_trylock(&input.mutex) == EINVAL);
    assert(ethr_event_destroy(&input.started) == 0);
    assert(ethr_event_destroy(&input.done) == 0);
    assert(ethr_mutex_init(&input.mutex) == 0);
    assert(ethr_mutex_destroy(&input.mutex) == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS MUTEX contended event-only wait and publication");
    puts("ERTS MUTEX trylock busy and destruction");
    puts("ERTS MUTEX PASS");
    return 0;
}
