/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "knytos.h"
#include "scheduler.h"
#include "smp.h"

enum { LOCK_TIMEOUT_MS = 5000 };
enum WorkerState { PARKED, SCHEDULE, STOP };

static struct SchedulerWorker {
    GlobalContext *global;
    unsigned slot;
    _Atomic enum WorkerState state;
} workers[AVM_KNYTOS_SCHEDULERS - 1];

static atomic_uint scheduled_workers;

static void require_ok(const char *operation, int status)
{
    if (status != KNYTOS_OK) {
        fprintf(stderr, "[atomvm] %s failed status=%d\n", operation, status);
        abort();
    }
}

static bool try_acquire(const char *operation, int status)
{
    if (status == KNYTOS_BUSY) {
        return false;
    }
    require_ok(operation, status);
    return true;
}

Mutex *smp_mutex_create(void)
{
    return (Mutex *) knytos_mutex_create();
}

void smp_mutex_destroy(Mutex *mutex)
{
    require_ok("mutex destroy", knytos_mutex_destroy((struct knytos_mutex *) mutex));
}

void smp_mutex_lock(Mutex *mutex)
{
    require_ok("mutex lock", knytos_mutex_lock((struct knytos_mutex *) mutex, LOCK_TIMEOUT_MS));
}

bool smp_mutex_trylock(Mutex *mutex)
{
    return try_acquire("mutex try-lock", knytos_mutex_try_lock((struct knytos_mutex *) mutex));
}

void smp_mutex_unlock(Mutex *mutex)
{
    require_ok("mutex unlock", knytos_mutex_unlock((struct knytos_mutex *) mutex));
}

CondVar *smp_condvar_create(void)
{
    return (CondVar *) knytos_condition_create();
}

void smp_condvar_destroy(CondVar *condition)
{
    require_ok("condition destroy", knytos_condition_destroy((struct knytos_condition *) condition));
}

void smp_condvar_wait(CondVar *condition, Mutex *mutex)
{
    require_ok("condition wait", knytos_condition_wait_event((struct knytos_condition *) condition,
        (struct knytos_mutex *) mutex, LOCK_TIMEOUT_MS));
}

void smp_condvar_signal(CondVar *condition)
{
    require_ok("condition signal", knytos_condition_signal((struct knytos_condition *) condition, 0));
}

RWLock *smp_rwlock_create(void)
{
    return (RWLock *) knytos_rwlock_create();
}

void smp_rwlock_destroy(RWLock *lock)
{
    require_ok("rwlock destroy", knytos_rwlock_destroy((struct knytos_rwlock *) lock));
}

void smp_rwlock_rdlock(RWLock *lock)
{
    require_ok("rwlock read", knytos_rwlock_read((struct knytos_rwlock *) lock, LOCK_TIMEOUT_MS));
}

bool smp_rwlock_tryrdlock(RWLock *lock)
{
    return try_acquire("rwlock try-read", knytos_rwlock_try_read((struct knytos_rwlock *) lock));
}

void smp_rwlock_wrlock(RWLock *lock)
{
    require_ok("rwlock write", knytos_rwlock_write((struct knytos_rwlock *) lock, LOCK_TIMEOUT_MS));
}

void smp_rwlock_unlock(RWLock *lock)
{
    require_ok("rwlock unlock", knytos_rwlock_unlock((struct knytos_rwlock *) lock));
}

int smp_get_online_processors(void)
{
    return AVM_KNYTOS_SCHEDULERS;
}

bool smp_is_main_thread(GlobalContext *global)
{
    (void) global;
    return knytos_thread_is_startup();
}

static void scheduler_worker(void *argument)
{
    struct SchedulerWorker *worker = argument;
    enum WorkerState state;
    while ((state = atomic_load_explicit(&worker->state, memory_order_acquire)) == PARKED) {
        require_ok("scheduler gate wait", knytos_thread_wait_event());
    }
    if (state == SCHEDULE) {
        printf("[atomvm] scheduler entered slot=%u\n", worker->slot);
        scheduler_entry_point(worker->global);
    }
}

void knytos_schedulers_prepare(GlobalContext *global)
{
    for (unsigned slot = 1; slot < AVM_KNYTOS_SCHEDULERS; ++slot) {
        struct SchedulerWorker *worker = &workers[slot - 1];
        worker->global = global;
        worker->slot = slot;
        require_ok("scheduler prepare", knytos_thread_start(slot, slot, scheduler_worker, worker));
    }
}

void smp_scheduler_start(GlobalContext *global)
{
    // The interpreter has already reserved this scheduler in its running
    // count. Only the fixed startup roster can satisfy that reservation.
    unsigned index = atomic_fetch_add_explicit(&scheduled_workers, 1, memory_order_relaxed);
    if (index >= AVM_KNYTOS_SCHEDULERS - 1 || workers[index].global != global) {
        require_ok("scheduler roster", KNYTOS_INVALID);
    }
    struct SchedulerWorker *worker = &workers[index];
    atomic_store_explicit(&worker->state, SCHEDULE, memory_order_release);
    require_ok("scheduler gate wake", knytos_thread_signal(worker->slot));
}

void knytos_schedulers_join(void)
{
    // The main interpreter has observed running_schedulers == 0. Any slot
    // still parked now was never selected. Active callbacks may still be
    // unwinding, so keep their VM context alive through exact join.
    for (unsigned slot = 1; slot < AVM_KNYTOS_SCHEDULERS; ++slot) {
        struct SchedulerWorker *worker = &workers[slot - 1];
        enum WorkerState expected = PARKED;
        if (atomic_compare_exchange_strong(&worker->state, &expected, STOP)) {
            require_ok("scheduler stop wake", knytos_thread_signal(slot));
        }
    }
    for (unsigned slot = 1; slot < AVM_KNYTOS_SCHEDULERS; ++slot) {
        require_ok("scheduler join", knytos_thread_join(slot));
    }
    printf("[atomvm] schedulers joined=%u activated=%u\n", AVM_KNYTOS_SCHEDULERS - 1,
        atomic_load_explicit(&scheduled_workers, memory_order_relaxed));
}
