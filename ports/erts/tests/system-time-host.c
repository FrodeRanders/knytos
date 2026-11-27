/* SPDX-License-Identifier: Apache-2.0 */
/* Private deterministic state access; this inclusion never enters the archive. */
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "../erts_system_time.c"

static atomic_int projection_error;
static int query_error;
static unsigned queries;
static int terminal, diagnosed;
static struct knytos_realtime_sample incoming;
static pthread_mutex_t pause_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_condition = PTHREAD_COND_INITIALIZER;
static int pause_phase;
static _Thread_local int pause_reader;

static struct knytos_realtime_sample make_sample(unsigned marker)
{
    return (struct knytos_realtime_sample){
        .utc_ns = 1000 + marker,
        .monotonic_ticks = marker * 3,
        .counter_frequency_hz = 1000000000 + marker,
        .drift_ppb = (int64_t)marker - 5000,
        .last_sync_age_ms = marker ^ 0x1234,
        .uncertainty_ms = marker + 7,
        .reserved = 0,
    };
}

static uint64_t limit(const struct knytos_realtime_sample *sample)
{
    return 1000000000 + sample->utc_ns;
}

int32_t knytos_clock_sample_project_ns(const struct knytos_realtime_sample *sample,
                                     uint64_t max_elapsed_ns, uint64_t *output)
{
    if (!sample) return KNYTOS_INVALID;
    if (pause_reader) {
        assert(!atomic_load(&readers)); /* The copied sample owns its bytes. */
        assert(pthread_mutex_lock(&pause_mutex) == 0);
        pause_phase = 1;
        assert(pthread_cond_signal(&pause_condition) == 0);
        while (pause_phase != 2) assert(pthread_cond_wait(&pause_condition, &pause_mutex) == 0);
        assert(pthread_mutex_unlock(&pause_mutex) == 0);
    }
    int error = atomic_load(&projection_error);
    if (error) return error;
    unsigned marker = (unsigned)(sample->utc_ns - 1000);
    /* Special numeric boundary samples are tested without marker metadata. */
    if (sample->utc_ns < 1000000) {
        struct knytos_realtime_sample expected = make_sample(marker);
        assert(memcmp(sample, &expected, sizeof(expected)) == 0);
    }
    assert(max_elapsed_ns == limit(sample)); /* Detect a torn sample/limit pair. */
    *output = sample->utc_ns;
    return KNYTOS_OK;
}

int32_t knytos_clock_realtime_sample(struct knytos_realtime_sample *output)
{
    assert(!atomic_load(&publishing));
    ++queries;
    if (query_error) return query_error;
    *output = incoming;
    return KNYTOS_OK;
}

int32_t knytos_clock_monotonic_ns(uint64_t *output)
{
    *output = 77;
    return KNYTOS_OK;
}

int32_t knytos_console_write(const void *bytes, size_t length)
{
    static const char expected[] = "[erts] system clock unavailable stale or out of range\n";
    assert(terminal && length == sizeof(expected) - 1 && memcmp(bytes, expected, length) == 0);
    diagnosed = 1;
    return KNYTOS_UNAVAILABLE;
}

_Noreturn void abort(void)
{
    _exit(terminal && diagnosed && !atomic_load(&readers) && !atomic_load(&publishing) ? 42 : 99);
}

static void *read_many(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 5000; ++i) {
        ErtsSystemTime value;
        assert(knytos_erts_clock_read(&value) == KNYTOS_OK);
        assert(value >= 1000 && value < 1000000);
    }
    return NULL;
}

static void *read_paused(void *argument)
{
    pause_reader = 1;
    assert(knytos_erts_clock_read(argument) == KNYTOS_OK);
    return NULL;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int mode = atoi(argv[1]);
    ErtsSystemTime output = -123;
    assert(knytos_erts_clock_read(&output) == KNYTOS_UNAVAILABLE && output == -123);
    assert(knytos_erts_clock_read(NULL) == KNYTOS_INVALID);
    _Alignas(8) unsigned char unaligned[16];
    assert(knytos_erts_clock_read((void *)(unaligned + 1)) == KNYTOS_INVALID);
    if (mode == 0) {
        terminal = 1;
        (void)erts_os_system_time();
        return 99;
    }
    struct knytos_realtime_sample first = make_sample(1), second = make_sample(2);
    assert(knytos_erts_clock_publish(NULL, 1) == KNYTOS_INVALID);
    assert(knytos_erts_clock_refresh(0) == KNYTOS_INVALID && queries == 0);
    assert(knytos_erts_clock_publish(&first, 0) == KNYTOS_INVALID);
    assert(knytos_erts_clock_publish(&first, limit(&first)) == KNYTOS_OK);
    assert(erts_os_system_time() == 1001);
    if (mode == 1) {
        ErtsMonotonicTime mono;
        erts_os_times(&mono, &output);
        assert(mono == 77 && output == 1001);
        assert(knytos_erts_clock_publish(&second, limit(&second)) == KNYTOS_OK);
        assert(erts_os_system_time() == 1002);
        /* Updates can step UTC backwards; there is no invented clamp. */
        assert(knytos_erts_clock_publish(&first, limit(&first)) == KNYTOS_OK);
        assert(erts_os_system_time() == 1001);
    } else if (mode == 2) {
        atomic_store(&readers, 1); /* Exact prior-reader pin, before reuse. */
        assert(knytos_erts_clock_publish(&second, limit(&second)) == KNYTOS_BUSY);
        assert(erts_os_system_time() == 1001);
        atomic_store(&readers, 0);
        atomic_store(&publishing, true); /* A publisher is preempted. */
        assert(knytos_erts_clock_publish(&second, limit(&second)) == KNYTOS_BUSY);
        assert(erts_os_system_time() == 1001); /* Readers do not wait for it. */
        atomic_store(&publishing, false);
    } else if (mode == 3) {
        query_error = 8;
        assert(knytos_erts_clock_refresh(limit(&second)) == 8 && queries == 1);
        assert(erts_os_system_time() == 1001);
        query_error = 0;
        incoming = second;
        assert(knytos_erts_clock_refresh(limit(&second)) == KNYTOS_OK && queries == 2);
        assert(erts_os_system_time() == 1002);
    } else if (mode == 4) {
        second.utc_ns = LONG_MAX;
        assert(knytos_erts_clock_publish(&second, limit(&second)) == KNYTOS_OK);
        assert(erts_os_system_time() == LONG_MAX);
        second.utc_ns++;
        assert(knytos_erts_clock_publish(&second, limit(&second)) == KNYTOS_OVERFLOW);
        assert(erts_os_system_time() == LONG_MAX);
    } else if (mode == 5) {
        for (int error = 1; error <= 9; ++error) {
            atomic_store(&projection_error, error);
            assert(knytos_erts_clock_publish(&second, limit(&second)) == error);
            assert(knytos_erts_clock_read(&output) == error && output == -123);
            atomic_store(&projection_error, 0);
            assert(erts_os_system_time() == 1001);
        }
    } else if (mode == 6) {
        atomic_store(&projection_error, KNYTOS_DEADLINE);
        assert(knytos_erts_clock_read(&output) == KNYTOS_DEADLINE && output == -123);
        terminal = 1;
        (void)erts_os_system_time();
        return 99;
    } else if (mode == 7) {
        pthread_t threads[3];
        for (unsigned i = 0; i < 3; ++i) assert(pthread_create(&threads[i], NULL, read_many, NULL) == 0);
        unsigned published = 0, busy = 0;
        /* Independent bounded admission attempts, no readiness polling or
         * replay of a pending operation. Rejection retains the prior sample. */
        for (unsigned i = 0; i < 5000; ++i) {
            struct knytos_realtime_sample sample = make_sample(i);
            int status = knytos_erts_clock_publish(&sample, limit(&sample));
            if (status == KNYTOS_OK) ++published;
            else { assert(status == KNYTOS_BUSY); ++busy; }
        }
        for (unsigned i = 0; i < 3; ++i) assert(pthread_join(threads[i], NULL) == 0);
        assert(published + busy == 5000 && !atomic_load(&readers) && !atomic_load(&publishing));
    } else if (mode == 8) {
        pthread_t thread;
        assert(pthread_mutex_lock(&pause_mutex) == 0);
        assert(pthread_create(&thread, NULL, read_paused, &output) == 0);
        while (pause_phase != 1) assert(pthread_cond_wait(&pause_condition, &pause_mutex) == 0);
        assert(knytos_erts_clock_publish(&second, limit(&second)) == KNYTOS_OK);
        struct knytos_realtime_sample third = make_sample(3);
        assert(knytos_erts_clock_publish(&third, limit(&third)) == KNYTOS_OK);
        /* Reuse the original slot while an old read projects its private
         * copy. Publication cannot alter or extend that captured sample. */
        pause_phase = 2;
        assert(pthread_cond_signal(&pause_condition) == 0);
        assert(pthread_mutex_unlock(&pause_mutex) == 0);
        assert(pthread_join(thread, NULL) == 0);
        assert(output == 1001 && erts_os_system_time() == 1003);
    } else return 99;
    return 0;
}
