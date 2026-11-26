/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>

#include "ethr_lock.h"

int ethr_spinlock_init(ethr_spinlock_t *lock)
{
    return ethr_mutex_init(lock);
}

int ethr_spinlock_destroy(ethr_spinlock_t *lock)
{
    return ethr_mutex_destroy(lock);
}

void ethr_spin_lock(ethr_spinlock_t *lock)
{
    ethr_mutex_lock(lock);
}

void ethr_spin_unlock(ethr_spinlock_t *lock)
{
    ethr_mutex_unlock(lock);
}

int ethr_rwlock_init(ethr_rwlock_t *lock)
{
    return ethr_rwmutex_init(lock);
}

int ethr_rwlock_destroy(ethr_rwlock_t *lock)
{
    return ethr_rwmutex_destroy(lock);
}

void ethr_read_lock(ethr_rwlock_t *lock)
{
    ethr_rwmutex_rlock(lock);
}

void ethr_read_unlock(ethr_rwlock_t *lock)
{
    ethr_rwmutex_runlock(lock);
}

void ethr_write_lock(ethr_rwlock_t *lock)
{
    ethr_rwmutex_rwlock(lock);
}

void ethr_write_unlock(ethr_rwlock_t *lock)
{
    ethr_rwmutex_rwunlock(lock);
}

void ethr_compiler_barrier(void)
{
    atomic_signal_fence(memory_order_seq_cst);
}
