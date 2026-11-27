/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdlib.h>

#include "ethr_mutex.h"

int ethr_mutex_lib_init(int cpu_configured)
{
    /* OTP uses this count to choose spin defaults and pthread attributes.
     * Native parking and writer admission are properties of each owned
     * object; neither requires a shared attribute or CPU-count snapshot. */
    (void)cpu_configured;
    return 0;
}

int ethr_mutex_lib_late_init(int reader_groups, int main_threads)
{
    /* The native rwlock has no reader-group arrays, and none of the native
     * locks spin. These hints do not describe kernel admission or placement.
     * In particular, repeat calls must not touch existing objects/waiters. */
    (void)reader_groups;
    (void)main_threads;
    return 0;
}

static void mutex_require_ok(int status)
{
    if (status != KNYTOS_OK) {
        static const char message[] = "[erts] mutex operation failed\n";
        knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
}

int ethr_mutex_init_opt(ethr_mutex *mutex, ethr_mutex_opt *options)
{
    if (!mutex || (options && (options->main_spincount < -1 || options->aux_spincount < -1)))
        return EINVAL;
    /* Spin counts are optional work before waiting. The native mutex parks
     * directly and supports callers using the POSIX-compliant convention. */
    mutex->native = knytos_mutex_create();
    return mutex->native ? 0 : ENOMEM;
}

int ethr_mutex_init(ethr_mutex *mutex)
{
    return ethr_mutex_init_opt(mutex, NULL);
}

int ethr_mutex_destroy(ethr_mutex *mutex)
{
    if (!mutex || !mutex->native) return EINVAL;
    int status = knytos_mutex_destroy(mutex->native);
    if (status == KNYTOS_BUSY) return EBUSY;
    mutex_require_ok(status);
    mutex->native = NULL;
    return 0;
}

int ethr_mutex_trylock(ethr_mutex *mutex)
{
    if (!mutex || !mutex->native) return EINVAL;
    int status = knytos_mutex_try_lock(mutex->native);
    /* Registered participants get INVALID for a recursive acquisition of
     * this nonrecursive mutex. ERTS trylock reports that held state as busy. */
    if (status == KNYTOS_BUSY || status == KNYTOS_INVALID) return EBUSY;
    mutex_require_ok(status);
    return 0;
}

void ethr_mutex_lock(ethr_mutex *mutex)
{
    mutex_require_ok(knytos_mutex_lock_event(mutex ? mutex->native : NULL));
}

void ethr_mutex_unlock(ethr_mutex *mutex)
{
    mutex_require_ok(knytos_mutex_unlock(mutex ? mutex->native : NULL));
}

static void condition_require_ok(int status)
{
    if (status != KNYTOS_OK) {
        static const char message[] = "[erts] condition operation failed\n";
        knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
}

int ethr_cond_init_opt(ethr_cond *condition, ethr_cond_opt *options)
{
    if (!condition || (options && (options->main_spincount < -1 || options->aux_spincount < -1)))
        return EINVAL;
    /* Native registration supports POSIX-compliant signaling without holding
     * the associated mutex. Optional spinning goes directly to that wait. */
    condition->native = knytos_condition_create();
    return condition->native ? 0 : ENOMEM;
}

int ethr_cond_init(ethr_cond *condition)
{
    return ethr_cond_init_opt(condition, NULL);
}

int ethr_cond_destroy(ethr_cond *condition)
{
    if (!condition || !condition->native) return EINVAL;
    int status = knytos_condition_destroy(condition->native);
    if (status == KNYTOS_BUSY) return EBUSY;
    condition_require_ok(status);
    condition->native = NULL;
    return 0;
}

void ethr_cond_signal(ethr_cond *condition)
{
    condition_require_ok(knytos_condition_signal(condition ? condition->native : NULL, 0));
}

void ethr_cond_broadcast(ethr_cond *condition)
{
    condition_require_ok(knytos_condition_signal(condition ? condition->native : NULL, 1));
}

int ethr_cond_wait(ethr_cond *condition, ethr_mutex *mutex)
{
    condition_require_ok(knytos_condition_wait_event_unbounded(
        condition ? condition->native : NULL, mutex ? mutex->native : NULL));
    return 0;
}
