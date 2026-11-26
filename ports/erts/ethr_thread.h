/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_THREAD_H
#define KNYTOS_ETHR_THREAD_H

#include <stdint.h>
#include <stddef.h>

/* Non-owning runtime identity. The adapter retains the native worker owner. */
typedef struct {
    uint32_t slot;
    uint64_t generation;
} ethr_tid;

typedef struct {
    int detached;
    int suggested_stack_size;
    char *name;
} ethr_thr_opts;

#define ETHR_THR_OPTS_DEFAULT_INITER {0, -1, NULL}
#define ETHR_THR_NAME_MAX 15

int ethr_thr_create(ethr_tid *tid, void *(*entry)(void *), void *argument, ethr_thr_opts *options);
int ethr_thr_join(ethr_tid tid, void **result);
int ethr_thr_detach(ethr_tid tid);
int ethr_install_exit_handler(void (*handler)(void));
_Noreturn void ethr_thr_exit(void *result);
ethr_tid ethr_self(void);
int ethr_equal_tids(ethr_tid first, ethr_tid second);
int ethr_getname(ethr_tid tid, char *buffer, size_t length);
void ethr_setname(char *name);
/* Startup fences creation and joins every retained worker. Callers first
 * publish terminal predicates and wakes, retaining arguments through success.
 * A failed observation retains the same records for a later drain call. */
int knytos_ethr_threads_shutdown(void);

#endif
