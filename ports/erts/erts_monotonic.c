/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "knytos.h"
#include "erts_monotonic.h"

static long read_nanoseconds(void)
{
    uint64_t value;
    if (knytos_clock_monotonic_ns(&value) != KNYTOS_OK || value > LONG_MAX) {
        static const char message[] = "[erts] monotonic clock unavailable or out of range\n";
        /* A failed diagnostic still terminates. No formatter, allocator,
         * finalizers or worker cleanup may run under an ERTS caller's lock. */
        (void)knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
    return (long)value;
}

ErtsMonotonicTime erts_os_monotonic_time(void)
{
    return read_nanoseconds();
}

ErtsSysHrTime erts_sys_hrtime(void)
{
    return read_nanoseconds();
}
