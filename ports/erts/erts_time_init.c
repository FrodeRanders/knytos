/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stdlib.h>
#include "erts_time_init.h"

#ifndef ERTS_KNYTOS_CLOCK_MAX_AGE_NS
#error "An explicit native clock sample-age policy is required"
#elif ERTS_KNYTOS_CLOCK_MAX_AGE_NS < 1
#error "The native clock sample-age policy must be nonzero"
#endif

static Uint64 resolution(uint64_t frequency, int64_t drift)
{
    /* Conservative maximum quantum: ceil(ns per counter tick), followed by
     * ceil(positive rate scaling). The source has already validated both.
     * Capped Hz describes numeric granularity, not NTP uncertainty. */
    uint64_t quantum = 1000000000 / frequency + (1000000000 % frequency != 0);
    unsigned __int128 scaled = (unsigned __int128)quantum
        * (unsigned __int128)((__int128)1000000000 + drift);
    scaled = scaled / 1000000000 + (scaled % 1000000000 != 0);
    if (scaled > 1000000000) return 1;
    return 1000000000 / (uint64_t)scaled;
}

int knytos_erts_init_time(ErtsSysInitTimeResult *output, uint64_t max_elapsed_ns)
{
    if (!output || (uintptr_t)output % _Alignof(ErtsSysInitTimeResult) || !max_elapsed_ns)
        return KNYTOS_INVALID;
    struct knytos_realtime_sample sample;
    int status = knytos_clock_realtime_sample(&sample);
    if (status != KNYTOS_OK) return status;
    /* Validate before division or record preparation. This also checks the
     * kernel's current frequency, sample age and timestamp arithmetic. */
    uint64_t projected;
    status = knytos_clock_sample_project_ns(&sample, max_elapsed_ns, &projected);
    if (status != KNYTOS_OK) return status;
    uint64_t monotonic;
    status = knytos_clock_monotonic_ns(&monotonic);
    if (status != KNYTOS_OK) return status;
    if (monotonic > LONG_MAX) return KNYTOS_OVERFLOW;
    ErtsSysInitTimeResult prepared = {
        .have_os_monotonic_time = 1,
        .have_corrected_os_monotonic_time = 0,
        .os_monotonic_time_unit = ERTS_COMPILE_TIME_MONOTONIC_TIME_UNIT,
        .sys_clock_resolution = SYS_CLOCK_RESOLUTION,
        .os_monotonic_time_info = {
            .resolution = resolution(sample.counter_frequency_hz, 0),
            .used_resolution = resolution(sample.counter_frequency_hz, 0),
            .func = "knytos_clock_monotonic_ns",
            .clock_id = NULL,
            .locked_use = 0,
            .extended = 0,
        },
        .os_system_time_info = {
            .resolution = resolution(sample.counter_frequency_hz, sample.drift_ppb),
            .func = "knytos_clock_sample_project_ns",
            .clock_id = NULL,
            .locked_use = 0,
        },
    };
    status = knytos_erts_clock_publish(&sample, max_elapsed_ns);
    if (status != KNYTOS_OK) return status;
    *output = prepared;
    return KNYTOS_OK;
}

void sys_init_time(ErtsSysInitTimeResult *output)
{
    if (knytos_erts_init_time(output, ERTS_KNYTOS_CLOCK_MAX_AGE_NS) != KNYTOS_OK) {
        static const char message[] = "[erts] clock initialization failed\n";
        (void)knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
}

void erts_late_sys_init_time(void)
{
    /* The native 64-bit counter needs no wrap extender. This hook installs
     * no refresh producer: the explicit startup sample-age limit still
     * applies until a caller publishes a new sample. */
}
