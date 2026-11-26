/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_MUTEX_H
#define KNYTOS_ETHR_MUTEX_H

#include "knytos.h"

typedef struct {
    struct knytos_mutex *native;
} ethr_mutex;

typedef struct {
    int main_spincount;
    int aux_spincount;
    int posix_compliant;
} ethr_mutex_opt;

#define ETHR_MUTEX_OPT_DEFAULT_INITER {-1, -1, 0}

int ethr_mutex_init_opt(ethr_mutex *mutex, ethr_mutex_opt *options);
int ethr_mutex_init(ethr_mutex *mutex);
int ethr_mutex_destroy(ethr_mutex *mutex);
int ethr_mutex_trylock(ethr_mutex *mutex);
void ethr_mutex_lock(ethr_mutex *mutex);
void ethr_mutex_unlock(ethr_mutex *mutex);

typedef struct {
    struct knytos_condition *native;
} ethr_cond;

typedef struct {
    int main_spincount;
    int aux_spincount;
    int posix_compliant;
} ethr_cond_opt;

#define ETHR_COND_OPT_DEFAULT_INITER {-1, -1, 0}

int ethr_cond_init_opt(ethr_cond *condition, ethr_cond_opt *options);
int ethr_cond_init(ethr_cond *condition);
int ethr_cond_destroy(ethr_cond *condition);
void ethr_cond_signal(ethr_cond *condition);
void ethr_cond_broadcast(ethr_cond *condition);
int ethr_cond_wait(ethr_cond *condition, ethr_mutex *mutex);

#endif
