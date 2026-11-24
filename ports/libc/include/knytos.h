#ifndef KNYTOS_C_ABI_H
#define KNYTOS_C_ABI_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { KNYTOS_OK = 0, KNYTOS_INVALID = 1, KNYTOS_UNAVAILABLE = 2, KNYTOS_OVERFLOW = 3 };
enum { KNYTOS_DEADLINE = 4, KNYTOS_WAIT_REJECTED = 5, KNYTOS_SHUTDOWN = 6 };
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
