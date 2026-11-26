/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"
#include "../ethr_mutex.c"

#ifndef CASE
#define CASE 53
#endif

struct input {
    ethr_mutex mutex;
    ethr_cond condition;
    ethr_event ready;
    ethr_event one;
    unsigned waiting;
    unsigned completed;
    unsigned phase;
};

static void *waiter(void *argument)
{
    struct input *input = argument;
    ethr_mutex_lock(&input->mutex);
    if (++input->waiting == 3) ethr_event_set(&input->ready);
    while (input->phase == 0)
        assert(ethr_cond_wait(&input->condition, &input->mutex) == 0);
    /* Successful wait owns the mutex. A recursive trylock must be busy. */
    assert(ethr_mutex_trylock(&input->mutex) == EBUSY);
    ++input->completed;
    ethr_event_set(&input->one);
    ethr_mutex_unlock(&input->mutex);
    return input;
}

int main(void)
{
    struct input input = {0};
    ethr_mutex_opt mutex_options = ETHR_MUTEX_OPT_DEFAULT_INITER;
    mutex_options.posix_compliant = 1;
    assert(ethr_mutex_init_opt(&input.mutex, &mutex_options) == 0);
    ethr_cond_opt options = ETHR_COND_OPT_DEFAULT_INITER;
    options.aux_spincount = -2;
    assert(ethr_cond_init_opt(&input.condition, &options) == EINVAL);
    assert(input.condition.native == NULL);
    options.main_spincount = 100;
    options.aux_spincount = 0;
    options.posix_compliant = 1;
    assert(ethr_cond_init_opt(&input.condition, &options) == 0);
    if (CASE == 54) ethr_cond_wait(&input.condition, &input.mutex);
    assert(ethr_event_init(&input.ready) == 0);
    assert(ethr_event_init(&input.one) == 0);
    /* Neither operation stores a token when there are no waiters. */
    ethr_cond_signal(&input.condition);
    ethr_cond_broadcast(&input.condition);
    ethr_tid tids[3];
    for (unsigned i = 0; i < 3; ++i)
        assert(ethr_thr_create(&tids[i], waiter, &input, NULL) == 0);
    assert(ethr_event_wait(&input.ready) == 0);
    /* Taking the mutex confirms all three registered before releasing it. */
    ethr_mutex_lock(&input.mutex);
    assert(input.waiting == 3 && input.completed == 0);
    assert(ethr_cond_destroy(&input.condition) == EBUSY);
    input.phase = 1;
    ethr_cond_signal(&input.condition);
    /* Selection cannot return until the application mutex is reacquired. */
    assert(ethr_event_twait(&input.one, 10000000) == ETIMEDOUT);
    ethr_mutex_unlock(&input.mutex);
    assert(ethr_event_wait(&input.one) == 0);
    ethr_mutex_lock(&input.mutex);
    assert(input.completed == 1);
    input.phase = 2;
    ethr_mutex_unlock(&input.mutex);
    /* POSIX-compliant signalers need not hold the associated mutex. */
    ethr_cond_broadcast(&input.condition);
    for (unsigned i = 0; i < 3; ++i) {
        void *result = NULL;
        assert(ethr_thr_join(tids[i], &result) == 0 && result == &input);
    }
    assert(input.completed == 3);
    assert(ethr_cond_destroy(&input.condition) == 0);
    assert(ethr_cond_destroy(&input.condition) == EINVAL);
    assert(ethr_cond_init(&input.condition) == 0);
    assert(ethr_cond_destroy(&input.condition) == 0);
    assert(ethr_mutex_destroy(&input.mutex) == 0);
    assert(ethr_event_destroy(&input.ready) == 0);
    assert(ethr_event_destroy(&input.one) == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS CONDITION signal selects one and broadcast releases peers");
    puts("ERTS CONDITION event-only reacquisition and busy destruction");
    puts("ERTS CONDITION PASS");
    return 0;
}
