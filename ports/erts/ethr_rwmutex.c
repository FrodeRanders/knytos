/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdlib.h>

#include "ethr_rwmutex.h"

static void rwmutex_require_ok(int status)
{
    if (status != KNYTOS_OK) {
        static const char message[] = "[erts] rwmutex operation failed\n";
        knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
}

int ethr_rwmutex_set_reader_group(int group)
{
    /* As with OTP's pthread backend, reader grouping is an unused hint. */
    (void)group;
    return 0;
}

int ethr_rwmutex_init_opt(ethr_rwmutex *lock, ethr_rwmutex_opt *options)
{
    if (!lock || (options && (options->type < ETHR_RWMUTEX_TYPE_NORMAL
        || options->type > ETHR_RWMUTEX_TYPE_EXTREMELY_FREQUENT_READ
        || options->lived < ETHR_RWMUTEX_LONG_LIVED
        || options->lived > ETHR_RWMUTEX_UNKNOWN_LIVED
        || options->main_spincount < -1 || options->aux_spincount < -1)))
        return EINVAL;
    /* Type, lifetime and spin options are optimization hints. All options
     * use the same registered native reader/writer algorithm. */
    lock->native = knytos_rwlock_create();
    return lock->native ? 0 : ENOMEM;
}

int ethr_rwmutex_init(ethr_rwmutex *lock)
{
    return ethr_rwmutex_init_opt(lock, NULL);
}

size_t ethr_rwmutex_size(ethr_rwmutex *lock)
{
    /* ERTS asks for storage in addition to its embedded wrapper. */
    return lock && lock->native ? knytos_rwlock_storage_size() : 0;
}

int ethr_rwmutex_destroy(ethr_rwmutex *lock)
{
    if (!lock || !lock->native) return EINVAL;
    int status = knytos_rwlock_destroy(lock->native);
    if (status == KNYTOS_BUSY) return EBUSY;
    rwmutex_require_ok(status);
    lock->native = NULL;
    return 0;
}

static int rwmutex_try_result(int status)
{
    if (status == KNYTOS_BUSY || status == KNYTOS_INVALID) return EBUSY;
    rwmutex_require_ok(status);
    return 0;
}

int ethr_rwmutex_tryrlock(ethr_rwmutex *lock)
{
    if (!lock || !lock->native) return EINVAL;
    return rwmutex_try_result(knytos_rwlock_try_read(lock->native));
}

int ethr_rwmutex_tryrwlock(ethr_rwmutex *lock)
{
    if (!lock || !lock->native) return EINVAL;
    return rwmutex_try_result(knytos_rwlock_try_write(lock->native));
}

void ethr_rwmutex_rlock(ethr_rwmutex *lock)
{
    rwmutex_require_ok(knytos_rwlock_read_event(lock ? lock->native : NULL));
}

void ethr_rwmutex_rwlock(ethr_rwmutex *lock)
{
    rwmutex_require_ok(knytos_rwlock_write_event(lock ? lock->native : NULL));
}

void ethr_rwmutex_runlock(ethr_rwmutex *lock)
{
    rwmutex_require_ok(knytos_rwlock_unlock(lock ? lock->native : NULL));
}

void ethr_rwmutex_rwunlock(ethr_rwmutex *lock)
{
    rwmutex_require_ok(knytos_rwlock_unlock(lock ? lock->native : NULL));
}
