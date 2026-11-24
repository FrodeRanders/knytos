/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avmpack.h"
#include "defaultatoms.h"
#include "platform_nifs.h"
#include "scheduler.h"
#include "sys.h"
#include "knytos.h"

static _Atomic uint64_t waits;
static _Atomic uint64_t deadlines;
static atomic_bool shutdown_pending;
#ifndef AVM_NO_SMP
static _Atomic uint64_t worker_waits;

void sys_signal(GlobalContext *global) {
    (void)global;
    knytos_event_signal();
}
#endif

static _Noreturn void unsupported(const char *operation) {
    fprintf(stderr, "[atomvm] unsupported: %s\n", operation);
    abort();
}

bool knytos_atomvm_shutdown_pending(void) {
    return atomic_load_explicit(&shutdown_pending, memory_order_acquire);
}

static void reactor_shutdown(GlobalContext *global, const char *message) {
    if (!atomic_exchange_explicit(&shutdown_pending, true, memory_order_acq_rel)) {
        knytos_console_write(message, strlen(message));
        scheduler_stop_all(global);
    }
}

void sys_init_platform(GlobalContext *global) { global->platform_data = NULL; }
void sys_free_platform(GlobalContext *global) {
    (void)global;
    printf("[atomvm] reactor waits=%llu deadlines=%llu\n",
        (unsigned long long)atomic_load_explicit(&waits, memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&deadlines, memory_order_relaxed));
#ifndef AVM_NO_SMP
    printf("[atomvm] reactor worker_waits=%llu\n",
        (unsigned long long)atomic_load_explicit(&worker_waits, memory_order_relaxed));
#endif
}
void platform_defaultatoms_init(GlobalContext *global) { (void)global; }

void sys_poll_events(GlobalContext *global, int timeout_ms) {
    if (timeout_ms == SYS_POLL_EVENTS_DO_NOT_WAIT) {
        // Continually runnable processes can keep the elected scheduler out
        // of CQ waits. This is an existing scheduling checkpoint outside the
        // VM's ready-list and scheduler locks.
        if (knytos_shutdown_requested()) {
            reactor_shutdown(global, "[atomvm] reactor shutdown source=nonblocking\n");
        }
        return;
    }
    atomic_fetch_add_explicit(&waits, 1, memory_order_relaxed);
#ifndef AVM_NO_SMP
    // AtomVM serializes this role with its scheduler mutex and publishes the
    // handoff only after returning from the wait. Private waiters use their
    // own CQs. At idle, the elected poller also observes lifecycle on CQ 0.
    if (!knytos_thread_is_startup()) atomic_fetch_add_explicit(&worker_waits, 1, memory_order_relaxed);
    int status = knytos_worker_event_wait(timeout_ms);
#else
    int status = knytos_event_wait(timeout_ms);
#endif
    if (status == KNYTOS_SHUTDOWN) {
        reactor_shutdown(global, "[atomvm] reactor shutdown source=cq-wait\n");
        return;
    }
    if (status == KNYTOS_DEADLINE) atomic_fetch_add_explicit(&deadlines, 1, memory_order_relaxed);
    else if (status != KNYTOS_OK) {
        fprintf(stderr, "[atomvm] reactor wait failed status=%d\n", status);
        abort();
    }
    /* The scheduler owns absolute timer expiry and rechecks runnable work on
     * each return. CQ readiness does not observe or release another owner. */
}

uint64_t sys_monotonic_time_u64(void) {
    uint64_t value;
    if (knytos_clock_monotonic_ns(&value) != KNYTOS_OK) abort();
    return value;
}
void sys_monotonic_time(struct timespec *time) {
    uint64_t now = sys_monotonic_time_u64();
    time->tv_sec = now / 1000000000;
    time->tv_nsec = now % 1000000000;
}
uint64_t sys_monotonic_time_ms_to_u64(uint64_t ms) {
    /* The pinned scheduler samples native_now before calling this hook and
     * then adds the returned duration. A later monotonic sample bounds both
     * the conversion and that addition without patching upstream code. */
    uint64_t now = sys_monotonic_time_u64();
    if (ms > (UINT64_MAX - now) / 1000000) unsupported("timer deadline overflow");
    return ms * 1000000;
}
uint64_t sys_monotonic_time_u64_to_ms(uint64_t time) {
    /* Round up: a submillisecond interval must not turn into a busy poll. */
    return time / 1000000 + (time % 1000000 != 0);
}
void sys_time(struct timespec *time) {
    (void)time;
    /* This upstream void hook cannot report unavailable. Until authorized UTC
     * has a reviewed quality/caching policy, fail explicitly rather than lie. */
    unsupported("wall clock unavailable");
}

enum OpenAVMResult sys_open_avm_from_file(GlobalContext *global, const char *path, struct AVMPackData **data) {
    (void)global; (void)path; (void)data;
    return AVM_OPEN_NOT_SUPPORTED;
}
Module *sys_load_module_from_file(GlobalContext *global, const char *path) {
    (void)global; (void)path; return NULL;
}
Module *sys_load_module(GlobalContext *global, const char *name) {
    const void *beam = NULL;
    uint32_t size = 0;
    struct ListHead *packs = synclist_wrlock(&global->avmpack_data);
    struct ListHead *item;
    LIST_FOR_EACH(item, packs) {
        struct AVMPackData *pack = GET_LIST_ENTRY(item, struct AVMPackData, avmpack_head);
        if (avmpack_find_section_by_name(pack->data, name, &beam, &size)) {
            pack->in_use = true;
            break;
        }
    }
    synclist_unlock(&global->avmpack_data);
    if (!beam) return NULL;
    Module *module = module_new_from_iff_binary(global, beam, size);
    if (module) module->module_platform_data = NULL;
    return module;
}
Context *sys_create_port(GlobalContext *global, const char *driver, term options) {
    (void)global; (void)driver; (void)options; return NULL;
}
term sys_get_info(Context *context, term key) {
    (void)context; (void)key; return UNDEFINED_ATOM;
}
void sys_register_select_event(GlobalContext *g, ErlNifEvent e, bool w) {
    (void)g; (void)e; (void)w; unsupported("select event");
}
void sys_unregister_select_event(GlobalContext *g, ErlNifEvent e, bool w) {
    (void)g; (void)e; (void)w; unsupported("select event");
}
void sys_register_listener(GlobalContext *g, EventListener *listener) {
    (void)g; (void)listener; unsupported("listener");
}
void sys_unregister_listener(GlobalContext *g, EventListener *listener) {
    (void)g; (void)listener; unsupported("listener");
}
void sys_listener_destroy(struct ListHead *item) { (void)item; unsupported("listener destroy"); }

static term platform(Context *context, int argc, term argv[]) {
    (void)argc; (void)argv;
    return globalcontext_make_atom(context->global, ATOM_STR("\6", "knytos"));
}
static const struct Nif platform_nif = { .base.type = NIFFunctionType, .nif_ptr = platform };
const struct Nif *platform_nifs_get_nif(const char *name) {
    return !strcmp(name, "atomvm:platform/0") ? &platform_nif : NULL;
}
