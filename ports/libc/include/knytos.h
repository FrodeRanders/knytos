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
struct knytos_condition;
struct knytos_mutex *knytos_mutex_create(void);
int32_t knytos_mutex_destroy(struct knytos_mutex *mutex);
int32_t knytos_mutex_try_lock(const struct knytos_mutex *mutex);
int32_t knytos_mutex_lock(const struct knytos_mutex *mutex, uint32_t timeout_ms);
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
/* all == 0 selects one registered waiter. Nonzero selects all. */
int32_t knytos_condition_signal(const struct knytos_condition *condition, uint32_t all);

struct knytos_rwlock;
struct knytos_rwlock *knytos_rwlock_create(void);
int32_t knytos_rwlock_destroy(struct knytos_rwlock *lock);
/* Shared acquisition may nest for the same participant. A queued writer
 * blocks new readers. Exclusive acquisition cannot nest or upgrade. */
int32_t knytos_rwlock_try_read(const struct knytos_rwlock *lock);
int32_t knytos_rwlock_read(const struct knytos_rwlock *lock, uint32_t timeout_ms);
int32_t knytos_rwlock_write(const struct knytos_rwlock *lock, uint32_t timeout_ms);
int32_t knytos_rwlock_unlock(const struct knytos_rwlock *lock);
#endif
#ifdef KNYTOS_SERVICE_WORKER
enum { KNYTOS_SERVICE_UNAUTHORIZED = 8, KNYTOS_SERVICE_UNEXPECTED_REPLY = 9 };
/* Worker-only scalar call using the signed scoped deployment grant.
 * The worker owns admission, observation and cleanup under one native budget.
 * The caller keeps the name and the separate output live until return. */
int32_t knytos_worker_call_scalar(const uint8_t *name, size_t length, uint32_t opcode,
                                  int64_t argument, int64_t *output);
#endif
struct knytos_allocation_stats { uint64_t blocks, requested_bytes, peak_requested_bytes; };
struct knytos_abi_pair { uint64_t unsigned_value; int64_t signed_value; double floating_value; };
uint32_t knytos_abi_version(void);
int32_t knytos_clock_monotonic_ns(uint64_t *result);
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
