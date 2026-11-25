/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "defaultatoms.h"
#include "erl_nif_priv.h"
#include "knytos.h"
#include "nifs.h"
#include "service_worker.h"

enum { WORKER_SLOT = 1, MAX_SERVICE_NAME = 128 };
enum RequestState { EMPTY, REQUESTED, RUNNING, REPLIED };

/* The scheduler and worker compete once for REQUESTED. Cancellation can
 * release an unclaimed slot. RUNNING keeps it reserved until the worker
 * disposes of its native owners and publishes REPLIED. */
static struct {
    _Atomic enum RequestState state;
    atomic_bool cancelled;
    // Borrowed by the sole scheduler until reply, cancellation or exit. The
    // worker never reads it. Termination clears it before the Context dies.
    Context *recipient;
    void *handle; // One AtomVM resource reference owned by this request.
    uint64_t reference;
    uint32_t kind;
    uint8_t service[MAX_SERVICE_NAME];
    size_t length;
    uint32_t opcode;
    int64_t argument;
    int32_t status;
    int64_t result;
} request;
static atomic_bool stopping;
static atomic_bool release_connection;
static bool cleanup_needs_signal;
static ErlNifResourceType *service_type;
// The scheduler owns these weak identities. A finalizer or process exit
// clears them before the corresponding allocation can be reused.
static struct {
    Context *owner;
    void *handle;
} session;

static void signal_worker(void)
{
    if (knytos_thread_signal(WORKER_SLOT) != KNYTOS_OK) abort();
}

static void release_request_handle(void)
{
    void *handle = request.handle;
    request.handle = NULL;
    if (handle) enif_release_resource(handle);
}

static void retire_session(void)
{
    session.owner = NULL;
    session.handle = NULL;
    atomic_store_explicit(&release_connection, true, memory_order_release);
    cleanup_needs_signal = true;
}

static void service_finalized(ErlNifEnv *env, void *handle)
{
    (void) env;
    // GC may hold VM guards. Publish to the pre-existing worker's custody.
    if (session.handle == handle) retire_session();
}

void knytos_service_flush_cleanup(void)
{
    if (cleanup_needs_signal) {
        cleanup_needs_signal = false;
        signal_worker();
    }
}

static bool cancellation_requested(void)
{
    return atomic_load_explicit(&request.cancelled, memory_order_acquire)
        || atomic_load_explicit(&stopping, memory_order_acquire);
}

static uint32_t next_command(struct knytos_service_command *command)
{
    while (!atomic_load_explicit(&stopping, memory_order_acquire)) {
        if (atomic_load_explicit(&release_connection, memory_order_acquire)) {
            return KNYTOS_SERVICE_RELEASE;
        }
        enum RequestState expected = REQUESTED;
        if (!atomic_compare_exchange_strong_explicit(&request.state, &expected, RUNNING,
                memory_order_acq_rel, memory_order_relaxed)) {
            if (knytos_thread_wait_event() != KNYTOS_OK) abort();
            continue;
        }
        *command = (struct knytos_service_command) {
            .name = request.service, .length = request.length,
            .argument = request.argument, .opcode = request.opcode,
        };
        return request.kind;
    }
    return KNYTOS_SERVICE_STOP;
}

static void complete_command(uint32_t kind, int32_t status, int64_t result)
{
    if (kind == KNYTOS_SERVICE_RELEASE) {
        atomic_store_explicit(&release_connection, false, memory_order_release);
    } else {
        request.status = status;
        request.result = result;
        atomic_store_explicit(&request.state, REPLIED, memory_order_release);
    }
    knytos_event_signal();
}

static void worker(void *argument)
{
    (void) argument;
    knytos_service_worker_run(next_command, complete_command, cancellation_requested);
}

void knytos_service_worker_start(GlobalContext *global)
{
    ErlNifEnv env;
    erl_nif_env_partial_init_from_globalcontext(&env, global);
    const ErlNifResourceTypeInit init = { .members = 1, .dtor = service_finalized };
    service_type = enif_init_resource_type(&env, "knytos_service", &init, ERL_NIF_RT_CREATE, NULL);
    if (!service_type || knytos_shutdown_via_reactor() != KNYTOS_OK
        || knytos_thread_start(WORKER_SLOT, 1, worker, NULL) != KNYTOS_OK) {
        abort();
    }
}

void knytos_service_worker_join(void)
{
    atomic_store_explicit(&stopping, true, memory_order_release);
    if (knytos_thread_signal(WORKER_SLOT) != KNYTOS_OK
        || knytos_thread_join(WORKER_SLOT) != KNYTOS_OK) {
        abort();
    }
    request.recipient = NULL;
    session.owner = NULL;
    session.handle = NULL;
    release_request_handle();
    atomic_store_explicit(&request.state, EMPTY, memory_order_release);
}

static term result_reason(GlobalContext *global, int status)
{
    switch (status) {
        case KNYTOS_BUSY: return globalcontext_make_atom(global, ATOM_STR("\4", "busy"));
        case KNYTOS_DEADLINE: return globalcontext_make_atom(global, ATOM_STR("\7", "timeout"));
        case KNYTOS_INVALID: return BADARG_ATOM;
        case KNYTOS_SERVICE_UNAUTHORIZED:
            return globalcontext_make_atom(global, ATOM_STR("\14", "unauthorized"));
        case KNYTOS_SERVICE_UNEXPECTED_REPLY:
            return globalcontext_make_atom(global, ATOM_STR("\20", "unexpected_reply"));
        case KNYTOS_WAIT_REJECTED:
            return globalcontext_make_atom(global, ATOM_STR("\15", "wait_rejected"));
        case KNYTOS_SHUTDOWN:
            return globalcontext_make_atom(global, ATOM_STR("\10", "shutdown"));
        case KNYTOS_SERVICE_NOT_FOUND:
            return globalcontext_make_atom(global, ATOM_STR("\11", "not_found"));
        default:
            return globalcontext_make_atom(global, ATOM_STR("\13", "unavailable"));
    }
}

static term result_pair(term tag, term value, Heap *heap)
{
    term result = term_alloc_tuple(2, heap);
    term_put_tuple_element(result, 0, tag);
    term_put_tuple_element(result, 1, value);
    return result;
}

static term error_reply(Context *ctx, int status)
{
    if (memory_ensure_free_opt(ctx, TUPLE_SIZE(2), MEMORY_CAN_SHRINK) != MEMORY_GC_OK) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    return result_pair(ERROR_ATOM, result_reason(ctx->global, status), &ctx->heap);
}

static bool request_busy(void)
{
    return atomic_load_explicit(&request.state, memory_order_acquire) != EMPTY;
}

static term queue_request(Context *ctx, uint32_t kind)
{
    if (memory_ensure_free_opt(ctx, TUPLE_SIZE(2) + REF_SIZE, MEMORY_CAN_SHRINK) != MEMORY_GC_OK) {
        release_request_handle();
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    request.kind = kind;
    request.recipient = ctx;
    request.reference = globalcontext_get_ref_ticks(ctx->global);
    term result = result_pair(OK_ATOM, term_from_ref_ticks(request.reference, &ctx->heap), &ctx->heap);
    atomic_store_explicit(&request.cancelled, false, memory_order_relaxed);
    atomic_store_explicit(&request.state, REQUESTED, memory_order_release);
    signal_worker();
    return result;
}

static bool owns_handle(Context *ctx, void *handle)
{
    return session.owner == ctx && session.handle == handle;
}

static term submit(Context *ctx, int argc, term argv[])
{
    (void) argc;
    VALIDATE_VALUE(argv[0], term_is_binary);
    VALIDATE_VALUE(argv[1], term_is_any_integer);
    VALIDATE_VALUE(argv[2], term_is_any_integer);
    int64_t opcode = term_maybe_unbox_int64(argv[1]);
    if (opcode < 0 || opcode > UINT32_MAX) {
        RAISE_ERROR(BADARG_ATOM);
    }
    void *handle = NULL;
    size_t length = term_binary_size(argv[0]);
    if (enif_get_resource(erl_nif_env_from_context(ctx), argv[0], service_type, &handle)) {
        if (!owns_handle(ctx, handle)) return error_reply(ctx, KNYTOS_SERVICE_NOT_FOUND);
    } else if (length == 0 || length > MAX_SERVICE_NAME) {
        RAISE_ERROR(BADARG_ATOM);
    }
    if (request_busy()) return error_reply(ctx, KNYTOS_BUSY);
    request.length = length;
    request.opcode = (uint32_t) opcode;
    request.argument = term_maybe_unbox_int64(argv[2]);
    if (handle) {
        enif_keep_resource(handle);
        request.handle = handle;
        return queue_request(ctx, KNYTOS_SERVICE_BOUND_CALL);
    }
    memcpy(request.service, term_binary_data(argv[0]), length);
    return queue_request(ctx, KNYTOS_SERVICE_NAMED_CALL);
}

const struct Nif knytos_request_nif = { .base.type = NIFFunctionType, .nif_ptr = submit };

static term lookup(Context *ctx, int argc, term argv[])
{
    (void) argc;
    VALIDATE_VALUE(argv[0], term_is_binary);
    size_t length = term_binary_size(argv[0]);
    if (length == 0 || length > MAX_SERVICE_NAME) {
        RAISE_ERROR(BADARG_ATOM);
    }
    if (request_busy() || session.handle || atomic_load_explicit(&release_connection, memory_order_acquire)) {
        return error_reply(ctx, KNYTOS_BUSY);
    }
    request.handle = enif_alloc_resource(service_type, 0);
    if (!request.handle) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    memcpy(request.service, term_binary_data(argv[0]), length);
    request.length = length;
    return queue_request(ctx, KNYTOS_SERVICE_LOOKUP);
}

const struct Nif knytos_lookup_nif = { .base.type = NIFFunctionType, .nif_ptr = lookup };

static term close_service(Context *ctx, int argc, term argv[])
{
    (void) argc;
    void *handle;
    if (!enif_get_resource(erl_nif_env_from_context(ctx), argv[0], service_type, &handle)) {
        RAISE_ERROR(BADARG_ATOM);
    }
    if (!owns_handle(ctx, handle)) return error_reply(ctx, KNYTOS_SERVICE_NOT_FOUND);
    if (request_busy()) return error_reply(ctx, KNYTOS_BUSY);
    retire_session();
    return OK_ATOM;
}

const struct Nif knytos_close_nif = { .base.type = NIFFunctionType, .nif_ptr = close_service };

static void abandon_request(void)
{
    atomic_store_explicit(&request.cancelled, true, memory_order_release);
    enum RequestState expected = REQUESTED;
    // Only the scheduler can publish another request. A worker that lost
    // this claim holds no borrowed slot data and can try the next request.
    if (atomic_compare_exchange_strong_explicit(&request.state, &expected, EMPTY,
            memory_order_acq_rel, memory_order_relaxed)) {
        request.recipient = NULL;
        release_request_handle();
    } else if (expected == RUNNING) {
        signal_worker();
    }
}

void knytos_service_owner_terminated(Context *owner)
{
    if (request.recipient == owner) {
        request.recipient = NULL;
        abandon_request();
    }
    if (session.owner == owner) retire_session();
}

static term cancel(Context *ctx, int argc, term argv[])
{
    (void) argc;
    VALIDATE_VALUE(argv[0], term_is_reference);
    if (request.recipient != ctx || request.reference != term_to_ref_ticks(argv[0])) {
        return error_reply(ctx, KNYTOS_SERVICE_NOT_FOUND);
    }

    abandon_request();
    return OK_ATOM;
}

const struct Nif knytos_cancel_nif = { .base.type = NIFFunctionType, .nif_ptr = cancel };

bool knytos_service_reply_ready(void)
{
    return atomic_load_explicit(&request.state, memory_order_acquire) == REPLIED;
}

void knytos_service_deliver(GlobalContext *global)
{
    if (!knytos_service_reply_ready()) return;
    Context *recipient = request.recipient;
    request.recipient = NULL;
    if (atomic_load_explicit(&request.cancelled, memory_order_acquire)) {
        if (request.kind == KNYTOS_SERVICE_LOOKUP && request.status == KNYTOS_OK) retire_session();
        release_request_handle();
        atomic_store_explicit(&request.state, EMPTY, memory_order_release);
        return;
    }

    BEGIN_WITH_STACK_HEAP(TUPLE_SIZE(3) + TUPLE_SIZE(2) + REF_SIZE
        + BOXED_INT64_SIZE + TERM_BOXED_RESOURCE_SIZE, heap);
    term payload;
    if (request.status != KNYTOS_OK) {
        payload = result_pair(ERROR_ATOM, result_reason(global, request.status), &heap);
    } else if (request.kind == KNYTOS_SERVICE_LOOKUP) {
        session.owner = recipient;
        session.handle = request.handle;
        payload = result_pair(OK_ATOM, term_from_resource(request.handle, &heap), &heap);
    } else {
        payload = result_pair(OK_ATOM, term_make_maybe_boxed_int64(request.result, &heap), &heap);
    }
    term message = term_alloc_tuple(3, &heap);
    term_put_tuple_element(message, 0, globalcontext_make_atom(global, ATOM_STR("\14", "knytos_reply")));
    term_put_tuple_element(message, 1, term_from_ref_ticks(request.reference, &heap));
    term_put_tuple_element(message, 2, payload);
    // Only the scheduler can terminate this recipient. Its exit hook
    // detaches it and suppresses delivery before it can be destroyed.
    globalcontext_send_message(global, recipient->process_id, message);
    END_WITH_STACK_HEAP(heap, global);
    release_request_handle();
    atomic_store_explicit(&request.state, EMPTY, memory_order_release);
}
