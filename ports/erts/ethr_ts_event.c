/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#include "ethr_ts_event.h"
#include "ethr_thread.h"
#include "ethr_bootstrap.h"

struct ts_node {
    ethr_ts_event public;
    struct ts_node *owned_next;
};

struct ts_context {
    ethr_tid participant;
    ethr_memory_allocator allocator;
    struct ts_node *owned;
    struct ts_node *cached;
    unsigned count;
};

/* One owner holds every fallible preparation until native event creation and
 * TSD publication succeed. An existing context is borrowed, never adopted. */
struct pending_event {
    ethr_memory_allocator allocator;
    struct ts_context *new_context;
    struct ts_node *node;
};

static atomic_uint_fast64_t context_key;
static atomic_int configured;
static atomic_uint used_events;
/* Copied before configured's release publication and immutable afterwards. */
static ethr_late_init_data event_config;
/* Each adapter slot has one current participant through exact native join.
 * Callback entry/return is synchronous; hooks must return normally. */
static unsigned allocator_active[KNYTOS_ETHR_MAIN_THREAD_LIMIT];

static void ts_require(int condition)
{
    if (!condition) {
        static const char message[] = "[erts] thread event lifecycle failed\n";
        knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
}

static unsigned *allocator_guard(void)
{
    ethr_tid tid = ethr_self();
    ts_require(tid.slot < KNYTOS_ETHR_MAIN_THREAD_LIMIT);
    return &allocator_active[tid.slot];
}

static void *allocate_zero(ethr_memory_allocator allocator, size_t bytes)
{
    unsigned *active = allocator_guard();
    ts_require(!*active);
    *active = 1;
    void *pointer = allocator.alloc(bytes);
    *active = 0;
    if (pointer) memset(pointer, 0, bytes);
    return pointer;
}

static void deallocate(ethr_memory_allocator allocator, void *pointer)
{
    if (!pointer) return;
    unsigned *active = allocator_guard();
    ts_require(!*active);
    *active = 1;
    allocator.free(pointer);
    *active = 0;
}

static struct ts_context *lookup_context(void)
{
    uint64_t key = atomic_load_explicit(&context_key, memory_order_acquire);
    struct ts_context *context = key ? knytos_tsd_get(key) : NULL;
    if (context) ts_require(ethr_equal_tids(context->participant, ethr_self()));
    return context;
}

static struct ts_context *current_context(void)
{
    ts_require(!*allocator_guard());
    return lookup_context();
}

static struct ts_node **find_node(struct ts_context *context, ethr_ts_event *event)
{
    if (!context) return NULL;
    struct ts_node **link = &context->owned;
    while (*link && &(*link)->public != event) link = &(*link)->owned_next;
    return *link ? link : NULL;
}

static int release_node(struct ts_context *context, struct ts_node **link)
{
    struct ts_node *node = *link;
    if ((node->public.iflgs & (ETHR_TS_EV_BUSY | ETHR_TS_EV_PEEK)) || node->public.udata)
        return EBUSY;
    int status = ethr_event_destroy(&node->public.event);
    if (status) return status;
    *link = node->owned_next;
    if (context->cached == node) context->cached = NULL;
    --context->count;
    deallocate(context->allocator, node);
    atomic_fetch_sub_explicit(&used_events, 1, memory_order_seq_cst);
    return 0;
}

static void discard_pending(struct pending_event *pending)
{
    if (pending->node) {
        if (pending->node->public.event.native)
            ts_require(ethr_event_destroy(&pending->node->public.event) == 0);
        deallocate(pending->allocator, pending->node);
        pending->node = NULL;
    }
    deallocate(pending->allocator, pending->new_context);
    pending->new_context = NULL;
}

static void context_destructor(void *argument)
{
    struct ts_context *context = argument;
    ts_require(ethr_equal_tids(context->participant, ethr_self()));
    /* The key is already clear. No table or roster guard encloses this
     * callback. A later user destructor may rearm a fresh context. */
    for (struct ts_node *node = context->owned; node; node = node->owned_next) {
        if ((node->public.iflgs & (ETHR_TS_EV_BUSY | ETHR_TS_EV_PEEK)) || node->public.udata) {
            static const char message[] = "[erts] thread event exit with live borrow\n";
            knytos_console_write(message, sizeof(message) - 1);
            ts_require(0);
        }
    }
    while (context->owned)
        ts_require(release_node(context, &context->owned) == 0);
    deallocate(context->allocator, context);
}

int knytos_ethr_ts_events_init(void)
{
    return knytos_ethr_ts_events_init_opt(NULL);
}

static int complete_allocator(ethr_memory_allocator allocator)
{
    return allocator.alloc && allocator.realloc && allocator.free;
}

static int prepare_config(ethr_late_init_data *prepared, const ethr_late_init_data *data)
{
    *prepared = data ? *data : (ethr_late_init_data) ETHR_LATE_INIT_DATA_DEFAULT_INITER;
    if (prepared->main_threads < 0 || prepared->reader_groups < 0) return EINVAL;
    if (prepared->main_threads > KNYTOS_ETHR_MAIN_THREAD_LIMIT) return ENOTSUP;
    /* Match upstream's whole-tuple fallback, never mix an allocator with
     * an unrelated deallocator from an incomplete callback group. */
    if (!complete_allocator(prepared->mem.std))
        prepared->mem.std = (ethr_memory_allocator) {malloc, realloc, free};
    if (!complete_allocator(prepared->mem.sl)) prepared->mem.sl = prepared->mem.std;
    if (!complete_allocator(prepared->mem.ll)) prepared->mem.ll = prepared->mem.std;
    return 0;
}

static int reserve_key(uint64_t *key)
{
    int status = knytos_tsd_key_create_with_destructor(key, context_destructor);
    if (status == KNYTOS_UNAVAILABLE) return EAGAIN;
    if (status == KNYTOS_OVERFLOW) return EOVERFLOW;
    if (status != KNYTOS_OK) return EINVAL;
    return 0;
}

int knytos_ethr_ts_events_reserve(void)
{
    if (!knytos_thread_is_startup()) return EACCES;
    if (atomic_load_explicit(&context_key, memory_order_acquire)) return EBUSY;
    uint64_t key;
    int status = reserve_key(&key);
    if (status) return status;
    atomic_store_explicit(&context_key, key, memory_order_release);
    return 0;
}

int knytos_ethr_ts_events_init_opt(const ethr_late_init_data *data)
{
    if (!knytos_thread_is_startup()) return EACCES;
    if (atomic_load_explicit(&context_key, memory_order_acquire)) return EBUSY;
    ethr_late_init_data prepared;
    int status = prepare_config(&prepared, data);
    if (status) return status;
    uint64_t key;
    status = reserve_key(&key);
    if (status) return status;
    event_config = prepared;
    atomic_store_explicit(&context_key, key, memory_order_release);
    atomic_store_explicit(&configured, 1, memory_order_release);
    return 0;
}

int knytos_ethr_ts_events_clear(void)
{
    uint64_t key = atomic_load_explicit(&context_key, memory_order_acquire);
    if (!key) return EACCES;
    if (*allocator_guard()) return EBUSY;
    struct ts_context *context = current_context();
    if (!context) return 0;
    while (context->owned) {
        int status = release_node(context, &context->owned);
        if (status) return status;
    }
    ts_require(knytos_tsd_set(key, NULL) == KNYTOS_OK);
    deallocate(context->allocator, context);
    return 0;
}

ethr_sint64_t ethr_no_used_tse(void)
{
    return atomic_load_explicit(&used_events, memory_order_seq_cst);
}

static int make_event(ethr_ts_event **output, int temporary, ethr_memory_allocator allocator)
{
    if (!output) return EINVAL;
    uint64_t key = atomic_load_explicit(&context_key, memory_order_acquire);
    if (!key) return EACCES;
    if (*allocator_guard()) return EBUSY;
    struct ts_context *context = current_context();
    struct ts_node **source = *output ? find_node(context, *output) : NULL;
    if (*output && (!source || !((*output)->iflgs & ETHR_TS_EV_BUSY))) return EINVAL;
    if (!*output && !temporary && context && context->cached) return EBUSY;
    if (context && context->count == KNYTOS_ETHR_TS_EVENT_LIMIT) return EAGAIN;
    struct pending_event pending = {
        .allocator = context ? context->allocator : allocator,
    };
    if (!context) {
        pending.new_context = allocate_zero(pending.allocator, sizeof(*context));
        if (!pending.new_context) return ENOMEM;
        context = pending.new_context;
        context->allocator = pending.allocator;
        context->participant = ethr_self();
    }
    pending.node = allocate_zero(pending.allocator, sizeof(*pending.node));
    if (!pending.node) {
        discard_pending(&pending);
        return ENOMEM;
    }
    ethr_ts_event *event = &pending.node->public;
    int status = ethr_event_init(&event->event);
    if (status) {
        discard_pending(&pending);
        return status;
    }
    if (source) {
        /* Copy borrowed queue metadata and flags, never the native owner. */
        ethr_ts_event *original = &(*source)->public;
        event->next = original->next;
        event->prev = original->prev;
        event->udata = original->udata;
        event->uflgs = original->uflgs;
        event->iflgs = (original->iflgs & ~ETHR_TS_EV_BUSY) | ETHR_TS_EV_TMP;
        event->rgix = original->rgix;
        event->mtix = original->mtix;
        ethr_atomic32_init(&event->uaflgs, ethr_atomic32_read(&original->uaflgs));
    } else {
        event->iflgs = ETHR_TS_EV_INITED | (temporary ? ETHR_TS_EV_TMP : ETHR_TS_EV_ETHREAD);
        ethr_atomic32_init(&event->uaflgs, 0);
    }
    if (pending.new_context && knytos_tsd_set(key, context) != KNYTOS_OK) {
        discard_pending(&pending);
        return EINVAL;
    }
    pending.node->owned_next = context->owned;
    context->owned = pending.node;
    ++context->count;
    if (!(event->iflgs & ETHR_TS_EV_TMP)) context->cached = pending.node;
    atomic_fetch_add_explicit(&used_events, 1, memory_order_seq_cst);
    pending.node = NULL;
    pending.new_context = NULL;
    *output = event;
    return 0;
}

int knytos_ethr_ts_events_start(const ethr_late_init_data *data)
{
    if (!knytos_thread_is_startup()) return EACCES;
    if (!atomic_load_explicit(&context_key, memory_order_acquire)) return EACCES;
    if (atomic_load_explicit(&configured, memory_order_acquire)) return EBUSY;
    if (*allocator_guard()) return EBUSY;
    ethr_late_init_data prepared;
    int status = prepare_config(&prepared, data);
    if (status) return status;
    ethr_ts_event *event = NULL;
    /* The common pending_event owns all allocation/event preparation. No
     * configuration is visible until the cached startup event is complete;
     * rejection rolls back through the same captured allocator. */
    status = make_event(&event, 0, prepared.mem.std);
    if (status) return status;
    event_config = prepared;
    atomic_store_explicit(&configured, 1, memory_order_release);
    return 0;
}

int ethr_make_ts_event__(ethr_ts_event **output, int temporary)
{
    if (!output) return EINVAL;
    if (!atomic_load_explicit(&context_key, memory_order_acquire)) return EACCES;
    if (*allocator_guard()) return EBUSY;
    if (!atomic_load_explicit(&configured, memory_order_acquire)) return EACCES;
    return make_event(output, temporary, event_config.mem.std);
}

int ethr_free_ts_event__(ethr_ts_event *event)
{
    if (*allocator_guard()) return EBUSY;
    struct ts_context *context = current_context();
    struct ts_node **link = find_node(context, event);
    return link ? release_node(context, link) : EINVAL;
}

int ethr_set_main_thr_status(int on, int number)
{
    if (*allocator_guard()) return EBUSY;
    struct ts_context *context = current_context();
    ethr_ts_event *event = context && context->cached ? &context->cached->public : NULL;
    if (!event) return EINVAL;
    if (on) {
        if (number < 1 || number > event_config.main_threads) return EINVAL;
        event->mtix = (short)number;
        event->iflgs |= ETHR_TS_EV_MAIN_THR;
    } else {
        event->mtix = 0;
        event->iflgs &= ~ETHR_TS_EV_MAIN_THR;
    }
    return 0;
}

int ethr_get_main_thr_status(int *on)
{
    if (!on) return EINVAL;
    /* Allocation callbacks may read status without acquiring a TSE borrow.
     * Release unlinks the node and clears cached before invoking free. */
    struct ts_context *context = lookup_context();
    *on = context && context->cached
        && (context->cached->public.iflgs & ETHR_TS_EV_MAIN_THR) ? 1 : 0;
    return 0;
}

ethr_ts_event *ethr_lookup_ts_event__(int busy_duplicate)
{
    struct ts_context *context = current_context();
    ethr_ts_event *event = context && context->cached ? &context->cached->public : NULL;
    if (!event || (busy_duplicate && (event->iflgs & ETHR_TS_EV_BUSY)))
        ts_require(ethr_make_ts_event__(&event, 0) == 0);
    return event;
}

ethr_ts_event *ethr_get_ts_event(void)
{
    ethr_ts_event *event = ethr_lookup_ts_event__(1);
    ts_require(!(event->iflgs & ETHR_TS_EV_BUSY));
    event->iflgs |= ETHR_TS_EV_BUSY;
    return event;
}

ethr_ts_event *ethr_peek_ts_event(void)
{
    ethr_ts_event *event = ethr_lookup_ts_event__(0);
    ts_require(!(event->iflgs & ETHR_TS_EV_PEEK));
    event->iflgs |= ETHR_TS_EV_PEEK;
    return event;
}

ethr_ts_event *ethr_use_ts_event(ethr_ts_event *event)
{
    ts_require(find_node(current_context(), event) != NULL);
    if (event->iflgs & ETHR_TS_EV_BUSY)
        ts_require(ethr_make_ts_event__(&event, 1) == 0);
    event->iflgs |= ETHR_TS_EV_BUSY;
    return event;
}

void ethr_leave_ts_event(ethr_ts_event *event)
{
    ts_require(find_node(current_context(), event) != NULL);
    ts_require(event->iflgs & ETHR_TS_EV_BUSY);
    event->iflgs &= ~ETHR_TS_EV_BUSY;
    if ((event->iflgs & (ETHR_TS_EV_TMP | ETHR_TS_EV_PEEK)) == ETHR_TS_EV_TMP)
        ts_require(ethr_free_ts_event__(event) == 0);
}

void ethr_unpeek_ts_event(ethr_ts_event *event)
{
    ts_require(find_node(current_context(), event) != NULL);
    ts_require(event->iflgs & ETHR_TS_EV_PEEK);
    event->iflgs &= ~ETHR_TS_EV_PEEK;
    if ((event->iflgs & (ETHR_TS_EV_TMP | ETHR_TS_EV_BUSY)) == ETHR_TS_EV_TMP)
        ts_require(ethr_free_ts_event__(event) == 0);
}
