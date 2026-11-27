/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "erts_time_init.h"

static struct knytos_realtime_sample incoming = {
    .utc_ns = 1700000000000000000ULL,
    .monotonic_ticks = 100,
    .counter_frequency_hz = 24000000,
    .drift_ppb = 500000,
    .last_sync_age_ms = 1,
};
static int query_error, project_error, publish_error;
static int monotonic_error;
static uint64_t monotonic_value = 123456789;
static unsigned queries, projects, publications;
static int terminal, diagnosed;
static ErtsSysInitTimeResult output, original;

int32_t knytos_clock_realtime_sample(struct knytos_realtime_sample *result)
{
    ++queries;
    if (query_error) return query_error;
    *result = incoming;
    return KNYTOS_OK;
}

int32_t knytos_clock_sample_project_ns(const struct knytos_realtime_sample *sample,
                                     uint64_t limit, uint64_t *result)
{
    ++projects;
    assert(limit && sample->utc_ns == incoming.utc_ns);
    if (project_error) return project_error;
    if (!sample->counter_frequency_hz || sample->drift_ppb <= -1000000000) return KNYTOS_INVALID;
    *result = sample->utc_ns;
    return KNYTOS_OK;
}

int knytos_erts_clock_publish(const struct knytos_realtime_sample *sample, uint64_t limit)
{
    assert(projects == queries && limit && memcmp(sample, &incoming, sizeof(incoming)) == 0);
    ++publications;
    return publish_error;
}

int32_t knytos_clock_monotonic_ns(uint64_t *result)
{
    if (monotonic_error) return monotonic_error;
    *result = monotonic_value;
    return KNYTOS_OK;
}

int32_t knytos_console_write(const void *bytes, size_t length)
{
    static const char expected[] = "[erts] clock initialization failed\n";
    assert(terminal && length == sizeof(expected) - 1 && !memcmp(bytes, expected, length));
    diagnosed = 1;
    return KNYTOS_UNAVAILABLE;
}

_Noreturn void abort(void)
{
    _exit(terminal && diagnosed && !memcmp(&output, &original, sizeof(output))
          && queries == 1 && !publications ? 42 : 99);
}

static void reset(void)
{
    queries = projects = publications = 0;
    memset(&output, 0xa5, sizeof(output));
    original = output;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int mode = atoi(argv[1]);
    reset();
    if (mode == 0) {
        sys_init_time(&output);
        assert(queries == 1 && projects == 1 && publications == 1);
        assert(output.have_os_monotonic_time == 1 && !output.have_corrected_os_monotonic_time);
        assert(output.os_monotonic_time_unit == 1000000000 && output.sys_clock_resolution == 1);
        assert(output.os_monotonic_time_info.resolution == 23809523);
        assert(output.os_monotonic_time_info.used_resolution == 23809523);
        assert(output.os_system_time_info.resolution == 23255813);
        assert(!output.os_monotonic_time_info.clock_id && !output.os_system_time_info.clock_id);
        assert(!output.os_monotonic_time_info.locked_use && !output.os_monotonic_time_info.extended);
        assert(!output.os_system_time_info.locked_use);
        assert(!strcmp(output.os_monotonic_time_info.func, "knytos_clock_monotonic_ns"));
        assert(!strcmp(output.os_system_time_info.func, "knytos_clock_sample_project_ns"));
        assert(erts_sys_perf_counter() == 123456789 && erts_sys_perf_counter_unit() == 1000000000);
        erts_late_sys_init_time();
        assert(queries == 1 && projects == 1 && publications == 1);
    } else if (mode == 1) {
        assert(knytos_erts_init_time(NULL, 1) == KNYTOS_INVALID);
        _Alignas(8) unsigned char bytes[sizeof(output) + 8];
        assert(knytos_erts_init_time((void *)(bytes + 1), 1) == KNYTOS_INVALID);
        assert(knytos_erts_init_time(&output, 0) == KNYTOS_INVALID);
        assert(!queries && !projects && !publications && !memcmp(&output, &original, sizeof(output)));
    } else if (mode >= 2 && mode <= 4) {
        for (int error = 1; error <= 9; ++error) {
            reset();
            query_error = mode == 2 ? error : 0;
            project_error = mode == 3 ? error : 0;
            publish_error = mode == 4 ? error : 0;
            assert(knytos_erts_init_time(&output, 1) == error);
            assert(!memcmp(&output, &original, sizeof(output)));
            assert(queries == 1 && projects == (mode != 2) && publications == (mode == 4));
        }
    } else if (mode == 5) {
        incoming.counter_frequency_hz = 0;
        assert(knytos_erts_init_time(&output, 1) == KNYTOS_INVALID && !publications);
        assert(!memcmp(&output, &original, sizeof(output)));
        incoming.counter_frequency_hz = 1;
        incoming.drift_ppb = INT64_MIN;
        assert(knytos_erts_init_time(&output, 1) == KNYTOS_INVALID && !publications);
    } else if (mode == 6) {
        terminal = 1;
        query_error = 8;
        sys_init_time(&output);
        return 99;
    } else if (mode == 7) {
        const struct {
            uint64_t frequency;
            int64_t drift;
            uint64_t mono_resolution, system_resolution;
        } cases[] = {
            {1000000000, 0, 1000000000, 1000000000},
            {1, 0, 1, 1},
            {1, -999999999, 1, 1000000000},
            {UINT64_MAX, INT64_MAX, 1000000000, 1},
            {1, INT64_MAX, 1, 1},
        };
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            reset();
            incoming.counter_frequency_hz = cases[i].frequency;
            incoming.drift_ppb = cases[i].drift;
            assert(knytos_erts_init_time(&output, 1) == KNYTOS_OK);
            assert(output.os_monotonic_time_info.resolution == cases[i].mono_resolution);
            assert(output.os_system_time_info.resolution == cases[i].system_resolution);
        }
    } else if (mode == 8) {
        for (int error = 1; error <= 9; ++error) {
            reset();
            monotonic_error = error;
            assert(knytos_erts_init_time(&output, 1) == error && !publications);
            assert(!memcmp(&output, &original, sizeof(output)));
        }
        reset();
        monotonic_error = 0;
        monotonic_value = LONG_MAX;
        assert(knytos_erts_init_time(&output, 1) == KNYTOS_OK && publications == 1);
        reset();
        monotonic_value++;
        assert(knytos_erts_init_time(&output, 1) == KNYTOS_OVERFLOW && !publications);
        assert(!memcmp(&output, &original, sizeof(output)));
    } else return 99;
    return 0;
}
