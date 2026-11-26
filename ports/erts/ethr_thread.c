/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#include "knytos.h"
#include "ethr_thread.h"

enum { THREAD_SLOTS = 3, EXIT_HANDLER_LIMIT = 16 };
static struct thread_record {
    int live;
    int detached;
    int running_exit_handlers;
    int exit_handlers_done;
    ethr_tid tid;
    void *(*entry)(void *);
    void *argument;
    void *result;
} threads[THREAD_SLOTS];
static uint64_t next_generation = 1;
static uint64_t identity_key;
static int creation_closed;
/* Startup appends immutable entries, then publishes their count. Each exit
 * captures one count before calling handlers in reverse registration order. */
static void (*exit_handlers[EXIT_HANDLER_LIMIT])(void);
static atomic_size_t exit_handler_count;

int ethr_install_exit_handler(void (*handler)(void))
{
    if (!knytos_thread_is_startup()) return EACCES;
    if (creation_closed) return ECANCELED;
    if (!handler) return EINVAL;
    size_t count = atomic_load_explicit(&exit_handler_count, memory_order_relaxed);
    if (count == EXIT_HANDLER_LIMIT) return EAGAIN;
    exit_handlers[count] = handler;
    atomic_store_explicit(&exit_handler_count, count + 1, memory_order_release);
    return 0;
}

static void run_exit_handlers(struct thread_record *record)
{
    if (record->running_exit_handlers) {
        static const char message[] = "[erts] recursive exit handler\n";
        knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
    if (record->exit_handlers_done) return;
    record->running_exit_handlers = 1;
    size_t count = atomic_load_explicit(&exit_handler_count, memory_order_acquire);
    while (count) exit_handlers[--count]();
    record->running_exit_handlers = 0;
    record->exit_handlers_done = 1;
}

/* Only the named participant writes its name after entry. Startup publishes
 * before native start and retires after exact join. Readers use atomic bytes
 * and one sequence check, returning EAGAIN on overlap without spinning. */
static struct thread_name {
    atomic_uint_fast64_t sequence;
    atomic_uint_fast64_t generation;
    atomic_uchar bytes[ETHR_THR_NAME_MAX + 1];
} names[THREAD_SLOTS + 1];

static size_t bounded_name_length(const char *name)
{
    size_t length = 0;
    while (name && length <= ETHR_THR_NAME_MAX && name[length]) length++;
    return length;
}

static void publish_name(ethr_tid tid, const char *name, size_t length)
{
    struct thread_name *target = &names[tid.slot];
    uint64_t sequence = atomic_load(&target->sequence);
    if (sequence > UINT64_MAX - 2) abort();
    atomic_store(&target->sequence, sequence + 1);
    for (size_t i = 0; i <= ETHR_THR_NAME_MAX; ++i)
        atomic_store(&target->bytes[i], i < length ? (unsigned char)name[i] : 0);
    atomic_store(&target->generation, tid.generation);
    atomic_store(&target->sequence, sequence + 2);
}

int ethr_getname(ethr_tid tid, char *buffer, size_t length)
{
    if (!buffer || !length || tid.slot > THREAD_SLOTS
        || (tid.slot == 0 && tid.generation != 0)
        || (tid.slot != 0 && tid.generation == 0)) return EINVAL;
    struct thread_name *target = &names[tid.slot];
    uint64_t sequence = atomic_load(&target->sequence);
    if (sequence & 1) return EAGAIN;
    uint64_t generation = atomic_load(&target->generation);
    char snapshot[ETHR_THR_NAME_MAX + 1];
    for (size_t i = 0; i < sizeof(snapshot); ++i)
        snapshot[i] = (char)atomic_load(&target->bytes[i]);
    if (atomic_load(&target->sequence) != sequence) return EAGAIN;
    if (generation != tid.generation) return EINVAL;
    size_t needed = strlen(snapshot) + 1;
    if (length < needed) return ERANGE;
    memcpy(buffer, snapshot, needed);
    return 0;
}

void ethr_setname(char *name)
{
    size_t length = bounded_name_length(name);
    if (length > ETHR_THR_NAME_MAX) return;
    publish_name(ethr_self(), name, length);
}

static int thread_status(int status)
{
    switch (status) {
        case KNYTOS_OK: return 0;
        case KNYTOS_UNAVAILABLE:
        case KNYTOS_BUSY:
        case KNYTOS_WAIT_REJECTED: return EAGAIN;
        case KNYTOS_OVERFLOW: return EOVERFLOW;
        case KNYTOS_SHUTDOWN: return ECANCELED;
        default: return EINVAL;
    }
}

static void enter_thread(void *argument)
{
    struct thread_record *record = argument;
    if (knytos_tsd_set(identity_key, record) != KNYTOS_OK) abort();
    record->result = record->entry(record->argument);
    run_exit_handlers(record);
}

_Noreturn void ethr_thr_exit(void *result)
{
    struct thread_record *record = knytos_tsd_get(identity_key);
    if (!record || knytos_thread_is_startup()) abort();
    record->result = result;
    run_exit_handlers(record);
    knytos_thread_exit();
}

int ethr_thr_create(ethr_tid *tid, void *(*entry)(void *), void *argument, ethr_thr_opts *options)
{
    if (!knytos_thread_is_startup()) return EACCES;
    if (creation_closed) return ECANCELED;
    if (!tid || !entry) return EINVAL;
    if (options && options->suggested_stack_size != -1) return ENOTSUP;
    const char *name = options ? options->name : NULL;
    size_t name_length = bounded_name_length(name);
    if (name_length > ETHR_THR_NAME_MAX) return EINVAL;
    unsigned index = 0;
    while (index < THREAD_SLOTS && threads[index].live) index++;
    if (index == THREAD_SLOTS) return EAGAIN;
    if (next_generation == UINT64_MAX) return EOVERFLOW;
    if (!identity_key) {
        int status = knytos_tsd_key_create(&identity_key);
        if (status != KNYTOS_OK) return thread_status(status);
    }
    struct thread_record *record = &threads[index];
    *record = (struct thread_record) {
        .live = 1, .detached = options && options->detached,
        .tid = {index + 1, next_generation++},
        .entry = entry, .argument = argument,
    };
    publish_name(record->tid, name, name_length);
    int status = knytos_thread_start(record->tid.slot, 1, enter_thread, record);
    if (status != KNYTOS_OK) {
        publish_name((ethr_tid) {record->tid.slot, 0}, NULL, 0);
        *record = (struct thread_record) {0};
        return thread_status(status);
    }
    *tid = record->tid;
    return 0;
}

int ethr_equal_tids(ethr_tid first, ethr_tid second)
{
    return first.slot == second.slot && first.generation == second.generation;
}

int ethr_thr_join(ethr_tid tid, void **result)
{
    if (!knytos_thread_is_startup()) return EACCES;
    if (!tid.slot || tid.slot > THREAD_SLOTS) return EINVAL;
    struct thread_record *record = &threads[tid.slot - 1];
    if (!record->live || record->detached || !ethr_equal_tids(record->tid, tid)) return EINVAL;
    int status = knytos_thread_join(tid.slot);
    if (status != KNYTOS_OK) return thread_status(status);
    if (result) *result = record->result;
    publish_name((ethr_tid) {tid.slot, 0}, NULL, 0);
    *record = (struct thread_record) {0};
    return 0;
}

int ethr_thr_detach(ethr_tid tid)
{
    if (!knytos_thread_is_startup()) return EACCES;
    if (!tid.slot || tid.slot > THREAD_SLOTS) return EINVAL;
    struct thread_record *record = &threads[tid.slot - 1];
    if (!record->live || record->detached || !ethr_equal_tids(record->tid, tid)) return EINVAL;
    record->detached = 1;
    return 0;
}

int knytos_ethr_threads_shutdown(void)
{
    if (!knytos_thread_is_startup()) return EACCES;
    creation_closed = 1;
    for (unsigned i = 0; i < THREAD_SLOTS; ++i) {
        struct thread_record *record = &threads[i];
        if (!record->live) continue;
        int status = knytos_thread_join(record->tid.slot);
        if (status != KNYTOS_OK) return thread_status(status);
        publish_name((ethr_tid) {record->tid.slot, 0}, NULL, 0);
        *record = (struct thread_record) {0};
    }
    return 0;
}

ethr_tid ethr_self(void)
{
    if (knytos_thread_is_startup()) return (ethr_tid) {0, 0};
    struct thread_record *record = knytos_tsd_get(identity_key);
    if (!record) abort();
    return record->tid;
}
