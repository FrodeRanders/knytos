/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "defaultatoms.h"
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
    int32_t recipient;
    uint64_t reference;
    uint8_t service[MAX_SERVICE_NAME];
    size_t length;
    uint32_t opcode;
    int64_t argument;
    int32_t status;
    int64_t result;
} request;
static atomic_bool stopping;

static bool cancellation_requested(void)
{
    return atomic_load_explicit(&request.cancelled, memory_order_acquire)
        || atomic_load_explicit(&stopping, memory_order_acquire);
}

static void worker(void *argument)
{
    (void) argument;
    while (!atomic_load_explicit(&stopping, memory_order_acquire)) {
        enum RequestState expected = REQUESTED;
        if (!atomic_compare_exchange_strong_explicit(&request.state, &expected, RUNNING,
                memory_order_acq_rel, memory_order_relaxed)) {
            if (knytos_thread_wait_event() != KNYTOS_OK) abort();
            continue;
        }
        request.status = knytos_worker_call_scalar(
            request.service, request.length, request.opcode, request.argument,
            cancellation_requested, &request.result);
        atomic_store_explicit(&request.state, REPLIED, memory_order_release);
        knytos_event_signal();
    }
}

void knytos_service_worker_start(void)
{
    if (knytos_shutdown_via_reactor() != KNYTOS_OK
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

static term submit(Context *ctx, int argc, term argv[])
{
    (void) argc;
    VALIDATE_VALUE(argv[0], term_is_binary);
    VALIDATE_VALUE(argv[1], term_is_any_integer);
    VALIDATE_VALUE(argv[2], term_is_any_integer);
    size_t length = term_binary_size(argv[0]);
    int64_t opcode = term_maybe_unbox_int64(argv[1]);
    if (length == 0 || length > MAX_SERVICE_NAME || opcode < 0 || opcode > UINT32_MAX) {
        RAISE_ERROR(BADARG_ATOM);
    }
    bool busy = atomic_load_explicit(&request.state, memory_order_acquire) != EMPTY;
    if (!busy) {
        memcpy(request.service, term_binary_data(argv[0]), length);
        request.length = length;
        request.opcode = (uint32_t) opcode;
        request.argument = term_maybe_unbox_int64(argv[2]);
        request.recipient = ctx->process_id;
    }
    if (memory_ensure_free_opt(ctx, TUPLE_SIZE(2) + REF_SIZE, MEMORY_CAN_SHRINK) != MEMORY_GC_OK) {
        RAISE_ERROR(OUT_OF_MEMORY_ATOM);
    }
    if (busy) {
        return result_pair(ERROR_ATOM, result_reason(ctx->global, KNYTOS_BUSY), &ctx->heap);
    }
    request.reference = globalcontext_get_ref_ticks(ctx->global);
    term result = result_pair(OK_ATOM, term_from_ref_ticks(request.reference, &ctx->heap), &ctx->heap);
    atomic_store_explicit(&request.cancelled, false, memory_order_relaxed);
    atomic_store_explicit(&request.state, REQUESTED, memory_order_release);
    if (knytos_thread_signal(WORKER_SLOT) != KNYTOS_OK) abort();
    return result;
}

const struct Nif knytos_request_nif = { .base.type = NIFFunctionType, .nif_ptr = submit };

static term cancel(Context *ctx, int argc, term argv[])
{
    (void) argc;
    VALIDATE_VALUE(argv[0], term_is_reference);
    if (atomic_load_explicit(&request.state, memory_order_acquire) == EMPTY
        || request.recipient != ctx->process_id
        || request.reference != term_to_ref_ticks(argv[0])) {
        if (memory_ensure_free_opt(ctx, TUPLE_SIZE(2), MEMORY_CAN_SHRINK) != MEMORY_GC_OK) {
            RAISE_ERROR(OUT_OF_MEMORY_ATOM);
        }
        term reason = globalcontext_make_atom(ctx->global, ATOM_STR("\11", "not_found"));
        return result_pair(ERROR_ATOM, reason, &ctx->heap);
    }

    atomic_store_explicit(&request.cancelled, true, memory_order_release);
    enum RequestState expected = REQUESTED;
    // Only the scheduler can publish another request. A worker that lost
    // this claim holds no borrowed slot data and can try the next request.
    if (!atomic_compare_exchange_strong_explicit(&request.state, &expected, EMPTY,
            memory_order_acq_rel, memory_order_relaxed)
        && expected == RUNNING && knytos_thread_signal(WORKER_SLOT) != KNYTOS_OK) {
        abort();
    }
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
    if (atomic_load_explicit(&request.cancelled, memory_order_acquire)) {
        atomic_store_explicit(&request.state, EMPTY, memory_order_release);
        return;
    }

    BEGIN_WITH_STACK_HEAP(TUPLE_SIZE(3) + TUPLE_SIZE(2) + REF_SIZE + BOXED_INT64_SIZE, heap);
    term payload = request.status == KNYTOS_OK
        ? result_pair(OK_ATOM, term_make_maybe_boxed_int64(request.result, &heap), &heap)
        : result_pair(ERROR_ATOM, result_reason(global, request.status), &heap);
    term message = term_alloc_tuple(3, &heap);
    term_put_tuple_element(message, 0, globalcontext_make_atom(global, ATOM_STR("\14", "knytos_reply")));
    term_put_tuple_element(message, 1, term_from_ref_ticks(request.reference, &heap));
    term_put_tuple_element(message, 2, payload);
    // The VM copies into a live recipient's mailbox and discards delivery
    // when the local PID has exited. The native operation is already settled.
    globalcontext_send_message(global, request.recipient, message);
    END_WITH_STACK_HEAP(heap, global);
    atomic_store_explicit(&request.state, EMPTY, memory_order_release);
}
