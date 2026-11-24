#ifndef KNYTOS_C_ABI_H
#define KNYTOS_C_ABI_H
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
int32_t knytos_thread_wait(uint32_t timeout_ms);
int32_t knytos_thread_signal(uint32_t slot);

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
int32_t knytos_allocation_stats(struct knytos_allocation_stats *result);
struct knytos_abi_pair knytos_abi_transform(struct knytos_abi_pair value, uint64_t delta);
/* Ordinary C pointer validity applies. No kernel capability is exposed. */
#ifdef __cplusplus
}
#endif
#endif
