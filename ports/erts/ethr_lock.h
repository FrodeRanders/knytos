/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_LOCK_H
#define KNYTOS_ETHR_LOCK_H

#include "ethr_mutex.h"
#include "ethr_rwmutex.h"

/* Blocking fallbacks, following OTP's mutex-backed lock interfaces. These
 * types do not advertise ETHR_HAVE_NATIVE_SPINLOCKS or RWSPINLOCKS. */
typedef ethr_mutex ethr_spinlock_t;
typedef ethr_rwmutex ethr_rwlock_t;

int ethr_spinlock_init(ethr_spinlock_t *lock);
int ethr_spinlock_destroy(ethr_spinlock_t *lock);
void ethr_spin_lock(ethr_spinlock_t *lock);
void ethr_spin_unlock(ethr_spinlock_t *lock);
int ethr_rwlock_init(ethr_rwlock_t *lock);
int ethr_rwlock_destroy(ethr_rwlock_t *lock);
void ethr_read_lock(ethr_rwlock_t *lock);
void ethr_read_unlock(ethr_rwlock_t *lock);
void ethr_write_lock(ethr_rwlock_t *lock);
void ethr_write_unlock(ethr_rwlock_t *lock);
void ethr_compiler_barrier(void);

#endif
