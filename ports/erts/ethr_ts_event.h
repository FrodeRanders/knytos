/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_TS_EVENT_H
#define KNYTOS_ETHR_TS_EVENT_H

#include <stddef.h>

#include "ethr_atomics.h"
#include "ethr_event.h"

typedef struct ethr_ts_event_ ethr_ts_event;
struct ethr_ts_event_ {
    ethr_ts_event *next;
    ethr_ts_event *prev;
    ethr_event event;
    void *udata;
    ethr_atomic32_t uaflgs;
    unsigned uflgs;
    unsigned iflgs;
    short rgix;
    short mtix;
};

#define ETHR_TS_EV_ETHREAD (1u << 0)
#define ETHR_TS_EV_INITED (1u << 1)
#define ETHR_TS_EV_TMP (1u << 2)
#define ETHR_TS_EV_MAIN_THR (1u << 3)
#define ETHR_TS_EV_BUSY (1u << 4)
#define ETHR_TS_EV_PEEK (1u << 5)
#define KNYTOS_ETHR_TS_EVENT_LIMIT 16
#define KNYTOS_ETHR_MAIN_THREAD_LIMIT 4

typedef struct {
    void *(*alloc)(size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
} ethr_memory_allocator;

#define ETHR_MEM_ALLOC_DEF_INITER__ {NULL, NULL, NULL}

typedef struct {
    ethr_memory_allocator std;
    ethr_memory_allocator sl;
    ethr_memory_allocator ll;
} ethr_memory_allocators;

#define ETHR_MEM_ALLOCS_DEF_INITER__ \
    {ETHR_MEM_ALLOC_DEF_INITER__, ETHR_MEM_ALLOC_DEF_INITER__, ETHR_MEM_ALLOC_DEF_INITER__}

typedef struct {
    ethr_memory_allocators mem;
    int reader_groups;
    int main_threads;
} ethr_late_init_data;

#define ETHR_LATE_INIT_DATA_DEFAULT_INITER {ETHR_MEM_ALLOCS_DEF_INITER__, 0, 0}

int ethr_late_init(ethr_late_init_data *data);

/* Startup initializes the reserved destructor key before publishing workers.
 * Clear operates on the calling participant and retains remaining objects
 * on busy rejection. Exclude event producers before destruction. */
int knytos_ethr_ts_events_init(void);
int knytos_ethr_ts_events_init_opt(const ethr_late_init_data *data);
int knytos_ethr_ts_events_clear(void);
ethr_sint64_t ethr_no_used_tse(void);
int ethr_make_ts_event__(ethr_ts_event **event, int temporary);
int ethr_free_ts_event__(ethr_ts_event *event);
ethr_ts_event *ethr_lookup_ts_event__(int busy_duplicate);
ethr_ts_event *ethr_get_ts_event(void);
ethr_ts_event *ethr_peek_ts_event(void);
ethr_ts_event *ethr_use_ts_event(ethr_ts_event *event);
void ethr_leave_ts_event(ethr_ts_event *event);
void ethr_unpeek_ts_event(ethr_ts_event *event);
int ethr_set_main_thr_status(int on, int number);
int ethr_get_main_thr_status(int *on);

#endif
