#ifndef KNYTOS_C_ABI_H
#define KNYTOS_C_ABI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { KNYTOS_OK = 0, KNYTOS_INVALID = 1, KNYTOS_UNAVAILABLE = 2, KNYTOS_OVERFLOW = 3 };
enum { KNYTOS_DEADLINE = 4, KNYTOS_WAIT_REJECTED = 5, KNYTOS_SHUTDOWN = 6 };
#ifdef KNYTOS_NATIVE_WORKERS
enum { KNYTOS_BUSY = 7 };
/* Worker slots are 1 through 3. Slot 0 denotes the startup thread. */
typedef void (*knytos_thread_entry)(void *);
int32_t knytos_thread_start(uint32_t slot, uint32_t lp, knytos_thread_entry entry, void *argument);
int32_t knytos_thread_join(uint32_t slot);
bool knytos_thread_is_startup(void);
/* Terminal worker exit runs TSD destructors before dropping participant
 * identity. No live Rust owner or guard may require stack unwinding.
 * Startup and recursive exit from TSD cleanup abort the domain. */
_Noreturn void knytos_thread_exit(void);
/* Up to 16 live keys. Keys are never reused. Values are borrowed pointers,
 * cleared at callback return or key deletion.
 * The caller retains pointees and synchronizes key deletion with users. */
int32_t knytos_tsd_key_create(uint64_t *key);
/* Optional callback for non-NULL values after worker callback return. Values
 * clear before invocation. Up to four passes allow callbacks to rearm values.
 * Further rearming aborts the domain. Retain callback code and pointees through
 * exit cleanup. Key deletion suppresses callbacks not yet captured by cleanup.
 * Startup TSD values require explicit cleanup before terminal C exit. */
typedef void (*knytos_tsd_destructor)(void *);
int32_t knytos_tsd_key_create_with_destructor(uint64_t *key, knytos_tsd_destructor destructor);
int32_t knytos_tsd_key_delete(uint64_t key);
int32_t knytos_tsd_set(uint64_t key, void *value);
/* NULL also denotes a stale key or a caller outside the worker roster. */
void *knytos_tsd_get(uint64_t key);
int32_t knytos_thread_wait(uint32_t timeout_ms);
/* Arrange an explicit terminal path before using an event-only private wait. */
int32_t knytos_thread_wait_event(void);
int32_t knytos_thread_signal(uint32_t slot);
/* The elected registered participant may borrow the sole CQ zero cursor.
 * An overlapping wait returns KNYTOS_BUSY. Same timeout units as event_wait. */
int32_t knytos_worker_event_wait(int64_t timeout_ms);
/* Startup can assign lifecycle delivery to CQ zero before starting workers.
 * Private synchronization then relies on peer readiness and terminal policy. */
int32_t knytos_shutdown_via_reactor(void);

struct knytos_mutex;
/* Manual-reset event, initially reset, with one waiter. Set persists until
 * reset. Keep it live through waits. Indefinite waits need a terminal path.
 * Destruction requires exclusive lifetime access and rejects a live waiter. */
struct knytos_event;
struct knytos_event *knytos_event_create(void);
int32_t knytos_event_destroy(struct knytos_event *event);
int32_t knytos_event_set(const struct knytos_event *event);
int32_t knytos_event_reset(const struct knytos_event *event);
/* -1 waits indefinitely, nonnegative milliseconds use one total deadline. */
int32_t knytos_event_wait_on(const struct knytos_event *event, int64_t timeout_ms);
struct knytos_condition;
struct knytos_mutex *knytos_mutex_create(void);
int32_t knytos_mutex_destroy(struct knytos_mutex *mutex);
int32_t knytos_mutex_try_lock(const struct knytos_mutex *mutex);
int32_t knytos_mutex_lock(const struct knytos_mutex *mutex, uint32_t timeout_ms);
/* Event-only acquisition requires a retained mutex, an unlock producer and
 * an explicit terminal path. Wait rejection and shutdown remain distinct. */
int32_t knytos_mutex_lock_event(const struct knytos_mutex *mutex);
int32_t knytos_mutex_unlock(const struct knytos_mutex *mutex);

struct knytos_condition *knytos_condition_create(void);
int32_t knytos_condition_destroy(struct knytos_condition *condition);
/* The caller owns mutex on entry. Only KNYTOS_OK reacquires it. The same
 * timeout budget covers the condition wait and the subsequent lock. */
int32_t knytos_condition_wait(const struct knytos_condition *condition,
                             const struct knytos_mutex *mutex, uint32_t timeout_ms);
/* Event-only condition phase with explicit terminal path. Reacquisition uses
 * its own timeout after selection. As above, only OK guarantees ownership. */
int32_t knytos_condition_wait_event(const struct knytos_condition *condition,
                                   const struct knytos_mutex *mutex, uint32_t reacquire_ms);
/* Selection and reacquisition both wait for events without deadlines. Retain
 * both objects and provide selection, unlock and shutdown progress. */
int32_t knytos_condition_wait_event_unbounded(const struct knytos_condition *condition,
                                             const struct knytos_mutex *mutex);
/* all == 0 selects one registered waiter. Nonzero selects all. */
int32_t knytos_condition_signal(const struct knytos_condition *condition, uint32_t all);

struct knytos_rwlock;
struct knytos_rwlock *knytos_rwlock_create(void);
/* Requested heap bytes behind the opaque object, excluding allocator overhead. */
size_t knytos_rwlock_storage_size(void);
int32_t knytos_rwlock_destroy(struct knytos_rwlock *lock);
/* Shared acquisition may nest for the same participant. A queued writer
 * blocks new readers. Exclusive acquisition cannot nest or upgrade. */
int32_t knytos_rwlock_try_read(const struct knytos_rwlock *lock);
int32_t knytos_rwlock_try_write(const struct knytos_rwlock *lock);
/* Both event-only acquisitions retain the lock through the wait. Provide
 * unlock producers and a terminal shutdown path before entering either. */
int32_t knytos_rwlock_read_event(const struct knytos_rwlock *lock);
int32_t knytos_rwlock_write_event(const struct knytos_rwlock *lock);
int32_t knytos_rwlock_read(const struct knytos_rwlock *lock, uint32_t timeout_ms);
int32_t knytos_rwlock_write(const struct knytos_rwlock *lock, uint32_t timeout_ms);
int32_t knytos_rwlock_unlock(const struct knytos_rwlock *lock);
#endif
#ifdef KNYTOS_SERVICE_WORKER
enum { KNYTOS_LOG_DEBUG, KNYTOS_LOG_INFO, KNYTOS_LOG_WARNING, KNYTOS_LOG_ERROR };
/* Write one diagnostic record in the caller's EL0 log context. */
int32_t knytos_atomvm_log(uint32_t level, const uint8_t *bytes, size_t length);
enum { KNYTOS_SERVICE_UNAUTHORIZED = 8, KNYTOS_SERVICE_UNEXPECTED_REPLY = 9 };
enum { KNYTOS_SERVICE_CANCELLED = 10, KNYTOS_SERVICE_NOT_FOUND = 11 };
enum { KNYTOS_SERVICE_BYTE_REPLY = 12, KNYTOS_SERVICE_MAX_PAYLOAD = 4096 };
enum { KNYTOS_SERVICE_STOP, KNYTOS_SERVICE_NAMED_CALL, KNYTOS_SERVICE_LOOKUP,
    KNYTOS_SERVICE_BOUND_CALL, KNYTOS_SERVICE_RELEASE, KNYTOS_SERVICE_SUBSCRIBE,
    KNYTOS_SERVICE_UNSUBSCRIBE };
struct knytos_service_command {
    const uint8_t *name;
    size_t length;
    int64_t argument;
    uint32_t opcode;
    uint8_t (*bytes)[KNYTOS_SERVICE_MAX_PAYLOAD];
    size_t bytes_len;
};
/* The worker owns all connections. next initializes a command for lookup
 * or call. The name stays live and the worker may overwrite bytes until
 * complete. A BYTE_REPLY result gives the initialized byte count. */
void knytos_service_worker_run(uint32_t (*next)(struct knytos_service_command *),
                               void (*complete)(uint32_t, int32_t, int64_t),
                               bool (*cancelled)(void));
/* Scheduler checkpoint on CQ zero. BUSY means the watch is still pending. */
int32_t knytos_service_watch_poll(void);
#endif
struct knytos_allocation_stats { uint64_t blocks, requested_bytes, peak_requested_bytes; };
struct knytos_abi_pair { uint64_t unsigned_value; int64_t signed_value; double floating_value; };
uint32_t knytos_abi_version(void);
int32_t knytos_clock_monotonic_ns(uint64_t *result);
/* Ordinary profiles return UNAVAILABLE. The service-worker profile queries
 * the signed time=call grant on the caller's private CQ with one 1 s budget.
 * Only synchronized snapshots produce UTC nanoseconds; all errors preserve
 * result. This can block and is not an async-signal or security-time API. */
int32_t knytos_clock_realtime_ns(uint64_t *result);
int32_t knytos_console_write(const void *buffer, size_t length);
void knytos_console_flush(void);
/* Sole reactor of launch CQ zero. -1 waits indefinitely; >=0 is milliseconds.
 * Readiness is a hint, not completion or ownership transfer. */
int32_t knytos_event_wait(int64_t timeout_ms);
void knytos_event_signal(void);
/* Read the authenticated lifecycle request and mark it seen when present. */
bool knytos_shutdown_requested(void);
/* Call from startup after exact worker joins and external resource disposal.
 * Runs finalizers and C heap cleanup before acknowledging a pending request. */
#ifdef __cplusplus
[[noreturn]] void knytos_shutdown_complete(void);
#else
_Noreturn void knytos_shutdown_complete(void);
#endif
int32_t knytos_allocation_stats(struct knytos_allocation_stats *result);
struct knytos_abi_pair knytos_abi_transform(struct knytos_abi_pair value, uint64_t delta);
/* Ordinary C pointer validity applies. No kernel capability is exposed. */
#ifdef __cplusplus
}
#endif
#endif
