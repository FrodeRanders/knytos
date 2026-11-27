/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "erts_system_time.h"

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_BOOL_LOCK_FREE == 2,
               "Clock publication requires native lock-free atomics");

struct snapshot {
    struct knytos_realtime_sample sample;
    uint64_t max_elapsed_ns;
};

static struct snapshot slots[2];
static atomic_uint active; /* 0 = uninitialized, 1/2 = slot 0/1. */
static atomic_uint readers;
static atomic_bool publishing;

/* All count/index operations are sequentially consistent. A reader pins
 * before loading active and releases only after copying the plain fields.
 * A serialized publisher requires a zero count before reusing the inactive
 * slot. Readers arriving after that check can only see the old active slot
 * or the completely published new one. A prior reader's release precedes
 * reuse through the acquired zero count. No slot is overwritten while read.
 * The native roster has startup plus at most three workers, no reentrant
 * reads/callbacks here, so the unsigned reader count cannot approach wrap. */
int knytos_erts_clock_read(ErtsSystemTime *output)
{
    if (!output || (uintptr_t)output % _Alignof(ErtsSystemTime)) return KNYTOS_INVALID;
    atomic_fetch_add(&readers, 1);
    unsigned index = atomic_load(&active);
    struct snapshot snapshot;
    if (index) snapshot = slots[index - 1];
    atomic_fetch_sub(&readers, 1);
    if (!index) return KNYTOS_UNAVAILABLE;
    uint64_t value;
    int status = knytos_clock_sample_project_ns(&snapshot.sample, snapshot.max_elapsed_ns, &value);
    if (status != KNYTOS_OK) return status;
    if (value > LONG_MAX) return KNYTOS_OVERFLOW;
    *output = (ErtsSystemTime)value;
    return KNYTOS_OK;
}

int knytos_erts_clock_publish(const struct knytos_realtime_sample *sample,
                             uint64_t max_elapsed_ns)
{
    if (!max_elapsed_ns) return KNYTOS_INVALID;
    uint64_t value;
    int status = knytos_clock_sample_project_ns(sample, max_elapsed_ns, &value);
    if (status != KNYTOS_OK) return status;
    if (value > LONG_MAX) return KNYTOS_OVERFLOW;
    bool expected = false;
    if (!atomic_compare_exchange_strong(&publishing, &expected, true)) return KNYTOS_BUSY;
    if (atomic_load(&readers)) {
        atomic_store(&publishing, false);
        return KNYTOS_BUSY;
    }
    unsigned target = atomic_load(&active) == 1 ? 1 : 0;
    slots[target] = (struct snapshot){.sample = *sample, .max_elapsed_ns = max_elapsed_ns};
    atomic_store(&active, target + 1);
    atomic_store(&publishing, false);
    return KNYTOS_OK;
}

int knytos_erts_clock_refresh(uint64_t max_elapsed_ns)
{
    if (!max_elapsed_ns) return KNYTOS_INVALID;
    struct knytos_realtime_sample sample;
    int status = knytos_clock_realtime_sample(&sample);
    if (status != KNYTOS_OK) return status;
    return knytos_erts_clock_publish(&sample, max_elapsed_ns);
}

ErtsSystemTime erts_os_system_time(void)
{
    ErtsSystemTime value;
    if (knytos_erts_clock_read(&value) != KNYTOS_OK) {
        static const char message[] = "[erts] system clock unavailable stale or out of range\n";
        (void)knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
    return value;
}

void erts_os_times(ErtsMonotonicTime *monotonic, ErtsSystemTime *system)
{
    *monotonic = erts_os_monotonic_time();
    *system = erts_os_system_time();
}
