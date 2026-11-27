/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ERTS_SYSTEM_TIME_H
#define KNYTOS_ERTS_SYSTEM_TIME_H

#include <stdint.h>
#include "knytos.h"
#include "erts_monotonic.h"

/* Standalone pinned LP64 surface; native sys/configure selection is separate. */
typedef ErtsMonotonicTime ErtsSystemTime;
ErtsSystemTime erts_os_system_time(void);
void erts_os_times(ErtsMonotonicTime *monotonic, ErtsSystemTime *system);

/* Publish a copied, immutable, non-owning sample. Both calls require a
 * nonzero nominal sample-age limit and preserve the active sample on error.
 * publish performs local validation only; it supplies no time authority.
 * refresh first acquires an authorized sample with the C ABI's blocking
 * budget. Call refresh outside ERTS locks and before first system-time use.
 * BUSY is unstarted publication admission; no automatic retry is performed.
 * Replacement steps UTC in either direction. Publication retains the supplied
 * counter anchor, never substituting publication time as the sample time.
 * Readers may finish using a still-valid prior snapshot during replacement. */
int knytos_erts_clock_publish(const struct knytos_realtime_sample *sample,
                             uint64_t max_elapsed_ns);
int knytos_erts_clock_refresh(uint64_t max_elapsed_ns);

/* Local status-bearing read; errors preserve output. No RPC, allocation,
 * lock acquisition, wait or retry. Output is disjoint from any input sample.
 * The statusless OTP getters abort on any failure, including expiry. The
 * paired getter uses sequential monotonic and system reads, as OTP's generic
 * fallback does; it is not a single-counter atomic pair. IRQ/signal/reentrant
 * use is outside the bounded native-worker profile. */
int knytos_erts_clock_read(ErtsSystemTime *output);

#endif
