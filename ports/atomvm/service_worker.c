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
    bool has_payload;
    size_t bytes_len;
    uint8_t bytes[KNYTOS_SERVICE_MAX_PAYLOAD];
    int32_t status;
    int64_t result;
} request;
static atomic_bool stopping;
static atomic_bool release_connection;
static atomic_bool release_watch;
static bool cleanup_needs_signal;
static ErlNifResourceType *service_type;
// The scheduler owns these weak identities. A finalizer or process exit
// clears them before the corresponding allocation can be reused.
static struct {
    Context *owner;
    void *handle;
} session;
static struct {
    Context *owner;
    uint64_t reference;
} subscription;

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

static void retire_watch(void)
{
    subscription.owner = NULL;
    atomic_store_explicit(&release_watch, true, memory_order_release);
    cleanup_needs_signal = true;
}

static void retire_session(void)
{
    if (subscription.owner) retire_watch();
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
        if (atomic_load_explicit(&release_watch, memory_order_acquire)) {
            return KNYTOS_SERVICE_UNSUBSCRIBE;
        }
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
            .bytes = request.has_payload ? &request.bytes : NULL,
            .bytes_len = request.bytes_len,
        };
        return request.kind;
    }
    return KNYTOS_SERVICE_STOP;
}

static void complete_command(uint32_t kind, int32_t status, int64_t result)
{
    if (kind == KNYTOS_SERVICE_RELEASE) {
        atomic_store_explicit(&release_connection, false, memory_order_release);
    } else if (kind == KNYTOS_SERVICE_UNSUBSCRIBE) {
        atomic_store_explicit(&release_watch, false, memory_order_release);
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
    subscription.owner = NULL;
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
    VALIDATE_VALUE(argv[0], term_is_binary);
    VALIDATE_VALUE(argv[1], term_is_any_integer);
    VALIDATE_VALUE(argv[2], term_is_any_integer);
    int64_t opcode = term_maybe_unbox_int64(argv[1]);
    if (opcode < 0 || opcode > UINT32_MAX) {
        RAISE_ERROR(BADARG_ATOM);
    }
    size_t bytes_len = 0;
    if (argc == 4) {
        VALIDATE_VALUE(argv[3], term_is_binary);
        bytes_len = term_binary_size(argv[3]);
        if (bytes_len > KNYTOS_SERVICE_MAX_PAYLOAD) {
            RAISE_ERROR(BADARG_ATOM);
        }
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
    request.has_payload = argc == 4;
    request.bytes_len = bytes_len;
    if (bytes_len != 0) memcpy(request.bytes, term_binary_data(argv[3]), bytes_len);
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
    request.has_payload = false;
    request.bytes_len = 0;
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

static term closed_atom(GlobalContext *global)
{
    return globalcontext_make_atom(global, ATOM_STR("\6", "closed"));
}

static term subscribe(Context *ctx, int argc, term argv[])
{
    (void) argc;
    void *handle;
    if (argv[1] != closed_atom(ctx->global)
        || !enif_get_resource(erl_nif_env_from_context(ctx), argv[0], service_type, &handle)) {
        RAISE_ERROR(BADARG_ATOM);
    }
    if (!owns_handle(ctx, handle)) return error_reply(ctx, KNYTOS_SERVICE_NOT_FOUND);
    if (request_busy() || subscription.owner || atomic_load_explicit(&release_watch, memory_order_acquire)) {
        return error_reply(ctx, KNYTOS_BUSY);
    }
    enif_keep_resource(handle);
    request.handle = handle;
    request.has_payload = false;
    request.bytes_len = 0;
    return queue_request(ctx, KNYTOS_SERVICE_SUBSCRIBE);
}

const struct Nif knytos_subscribe_nif = { .base.type = NIFFunctionType, .nif_ptr = subscribe };

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

static term unsubscribe(Context *ctx, int argc, term argv[])
{
    (void) argc;
    VALIDATE_VALUE(argv[0], term_is_reference);
    uint64_t reference = term_to_ref_ticks(argv[0]);
    if (subscription.owner == ctx && subscription.reference == reference) {
        retire_watch();
    } else if (request.kind == KNYTOS_SERVICE_SUBSCRIBE && request.recipient == ctx
        && request.reference == reference) {
        abandon_request();
    } else {
        return error_reply(ctx, KNYTOS_SERVICE_NOT_FOUND);
    }
    return OK_ATOM;
}

const struct Nif knytos_unsubscribe_nif = { .base.type = NIFFunctionType, .nif_ptr = unsubscribe };

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

static bool reply_ready(void)
{
    return atomic_load_explicit(&request.state, memory_order_acquire) == REPLIED;
}

bool knytos_service_needs_scan(void)
{
    return reply_ready() || subscription.owner != NULL;
}

static bool deliver_reply(GlobalContext *global)
{
    if (!reply_ready()) return false;
    Context *recipient = request.recipient;
    request.recipient = NULL;
    if (atomic_load_explicit(&request.cancelled, memory_order_acquire)) {
        if (request.kind == KNYTOS_SERVICE_LOOKUP && request.status == KNYTOS_OK) retire_session();
        if (request.kind == KNYTOS_SERVICE_SUBSCRIBE && request.status == KNYTOS_OK) retire_watch();
        release_request_handle();
        atomic_store_explicit(&request.state, EMPTY, memory_order_release);
        return true;
    }

    bool byte_reply = request.status == KNYTOS_SERVICE_BYTE_REPLY;
    size_t bytes_len = byte_reply ? (size_t) request.result : 0;
    if (bytes_len > KNYTOS_SERVICE_MAX_PAYLOAD) abort();
    // The stack heap needs constant storage for the largest inline binary
    // or the refcounted header. Larger byte buffers are allocated by AtomVM.
    BEGIN_WITH_STACK_HEAP(TUPLE_SIZE(3) + TUPLE_SIZE(2) + REF_SIZE
        + BOXED_INT64_SIZE + TERM_BOXED_RESOURCE_SIZE
        + BINARY_HEADER_SIZE + REFC_BINARY_MIN / TERM_BYTES, heap);
    term payload;
    if (byte_reply) {
        term binary = term_from_literal_binary(request.bytes, bytes_len, &heap, global);
        payload = result_pair(OK_ATOM, binary, &heap);
    } else if (request.status != KNYTOS_OK) {
        payload = result_pair(ERROR_ATOM, result_reason(global, request.status), &heap);
    } else if (request.kind == KNYTOS_SERVICE_LOOKUP) {
        session.owner = recipient;
        session.handle = request.handle;
        payload = result_pair(OK_ATOM, term_from_resource(request.handle, &heap), &heap);
    } else if (request.kind == KNYTOS_SERVICE_SUBSCRIBE) {
        subscription.owner = recipient;
        subscription.reference = request.reference;
        payload = OK_ATOM;
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
    return true;
}

static bool deliver_watch(GlobalContext *global)
{
    Context *recipient = subscription.owner;
    if (!recipient) return false;
    int status = knytos_service_watch_poll();
    if (status == KNYTOS_BUSY) return false;

    uint64_t reference = subscription.reference;
    if (status == KNYTOS_OK) {
        retire_session();
    } else {
        retire_watch();
    }
    BEGIN_WITH_STACK_HEAP(TUPLE_SIZE(3) + TUPLE_SIZE(2) + REF_SIZE, heap);
    term event = status == KNYTOS_OK ? closed_atom(global)
        : result_pair(ERROR_ATOM, result_reason(global, status), &heap);
    term message = term_alloc_tuple(3, &heap);
    term_put_tuple_element(message, 0, globalcontext_make_atom(global, ATOM_STR("\14", "knytos_event")));
    term_put_tuple_element(message, 1, term_from_ref_ticks(reference, &heap));
    term_put_tuple_element(message, 2, event);
    globalcontext_send_message(global, recipient->process_id, message);
    END_WITH_STACK_HEAP(heap, global);
    return true;
}

bool knytos_service_deliver(GlobalContext *global)
{
    bool replied = deliver_reply(global);
    return deliver_watch(global) || replied;
}
