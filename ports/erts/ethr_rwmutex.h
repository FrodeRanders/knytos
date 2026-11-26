/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_RWMUTEX_H
#define KNYTOS_ETHR_RWMUTEX_H

#include "knytos.h"

typedef struct {
    struct knytos_rwlock *native;
} ethr_rwmutex;

typedef enum {
    ETHR_RWMUTEX_TYPE_NORMAL,
    ETHR_RWMUTEX_TYPE_FREQUENT_READ,
    ETHR_RWMUTEX_TYPE_EXTREMELY_FREQUENT_READ
} ethr_rwmutex_type;

typedef enum {
    ETHR_RWMUTEX_LONG_LIVED,
    ETHR_RWMUTEX_SHORT_LIVED,
    ETHR_RWMUTEX_UNKNOWN_LIVED
} ethr_rwmutex_lived;

typedef struct {
    ethr_rwmutex_type type;
    ethr_rwmutex_lived lived;
    int main_spincount;
    int aux_spincount;
} ethr_rwmutex_opt;

#define ETHR_RWMUTEX_OPT_DEFAULT_INITER \
    {ETHR_RWMUTEX_TYPE_NORMAL, ETHR_RWMUTEX_UNKNOWN_LIVED, -1, -1}

int ethr_rwmutex_set_reader_group(int group);
int ethr_rwmutex_init_opt(ethr_rwmutex *lock, ethr_rwmutex_opt *options);
int ethr_rwmutex_init(ethr_rwmutex *lock);
size_t ethr_rwmutex_size(ethr_rwmutex *lock);
int ethr_rwmutex_destroy(ethr_rwmutex *lock);
int ethr_rwmutex_tryrlock(ethr_rwmutex *lock);
void ethr_rwmutex_rlock(ethr_rwmutex *lock);
void ethr_rwmutex_runlock(ethr_rwmutex *lock);
int ethr_rwmutex_tryrwlock(ethr_rwmutex *lock);
void ethr_rwmutex_rwlock(ethr_rwmutex *lock);
void ethr_rwmutex_rwunlock(ethr_rwmutex *lock);

#endif
