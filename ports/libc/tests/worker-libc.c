#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "knytos.h"

#ifndef CASE
#define CASE 19
#endif

struct shared {
    uint64_t key;
    uint64_t deleted_key;
    atomic_uint entered;
    atomic_uint complete;
    atomic_uint reset;
    atomic_uintptr_t errno_address;
};

static void libc_worker(void *argument)
{
    struct shared *s = argument;
    assert(knytos_tsd_get(s->key) == NULL);
    assert(knytos_tsd_set(s->key, &s->complete) == KNYTOS_OK);
    assert(knytos_tsd_get(s->key) == &s->complete);
    assert(knytos_tsd_set(s->deleted_key, s) == KNYTOS_OK);
    int valid = errno == 0;
    volatile size_t impossible = SIZE_MAX;
    void *rejected = calloc(impossible, 2);
    valid = valid && rejected == NULL && errno == ENOMEM;
    free(rejected);
    errno = 0;
    (void)strtol("999999999999999999999999", NULL, 10);
    valid = valid && errno == ERANGE;

    atomic_store_explicit(&s->errno_address, (uintptr_t)&errno, memory_order_relaxed);
    atomic_store_explicit(&s->entered, valid ? 1 : 2, memory_order_release);
    knytos_thread_signal(0);
    /* Startup already has the output lock. stderr uses the same stream. */
    valid = fprintf(stderr, "WORKER LIBC worker acquired stream\n") > 0 && valid;
    assert(knytos_tsd_get(s->deleted_key) == NULL);
    assert(knytos_tsd_set(s->deleted_key, s) == KNYTOS_INVALID);
    assert(knytos_tsd_get(s->key) == &s->complete);
    for (unsigned i = 0; i < 8; ++i) {
        char *buffer = malloc(64);
        if (!buffer) {
            valid = 0;
            break;
        }
        int formatted = snprintf(buffer, 64, "row=%u text=%s", i, "clear");
        valid = formatted > 0 && (size_t)formatted == strlen(buffer) && valid;
        valid = fprintf(stderr, "WORKER LIBC worker %s\n", buffer) > 0 && valid;
        free(buffer);
    }
    errno = EINTR;
    atomic_store_explicit(&s->complete, valid ? 1 : 2, memory_order_release);
}

static void different_generation(void *argument)
{
    struct shared *s = argument;
    assert(knytos_tsd_get(s->key) == NULL);
    assert(knytos_tsd_set(s->key, &s->reset) == KNYTOS_OK);
    int fresh = errno == 0;
    errno = EAGAIN;
    atomic_store_explicit(&s->reset, fresh ? 1 : 2, memory_order_release);
}

static unsigned await_entry(struct shared *s)
{
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    const uint64_t until = now + 2000000000;
    for (;;) {
        unsigned entered = atomic_load_explicit(&s->entered, memory_order_acquire);
        if (entered)
            return entered;
        assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK && now < until);
        int status = knytos_thread_wait((uint32_t)((until - now + 999999) / 1000000));
        assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
    }
}

int main(void)
{
    if (CASE == 20) {
        puts("WORKER LIBC expected fixed-stream close rejection");
        fclose(stdout);
        return 1;
    }

    struct shared s = {0};
    assert(knytos_tsd_key_create(NULL) == KNYTOS_INVALID);
    assert(knytos_tsd_key_create(&s.key) == KNYTOS_OK);
    assert(knytos_tsd_key_create(&s.deleted_key) == KNYTOS_OK);
    assert(knytos_tsd_get(s.key) == NULL);
    assert(knytos_tsd_set(s.key, &s.entered) == KNYTOS_OK);
    errno = EDOM;
    flockfile(stdout);
    flockfile(stderr);
    assert(fputs("WORKER LIBC held ", stdout) >= 0);
    assert(knytos_thread_start(1, 1, libc_worker, &s) == KNYTOS_OK);
    assert(await_entry(&s) == 1);
    assert(knytos_tsd_get(s.key) == &s.entered);
    assert(knytos_tsd_key_delete(s.deleted_key) == KNYTOS_OK);
    uint64_t replacement;
    assert(knytos_tsd_key_create(&replacement) == KNYTOS_OK);
    assert(replacement != s.deleted_key && knytos_tsd_get(replacement) == NULL);
    assert(errno == EDOM);
    assert(atomic_load(&s.errno_address) != (uintptr_t)&errno);
    funlockfile(stdout);
    assert(atomic_load(&s.complete) == 0);
    assert(fputs("record\n", stderr) >= 0);
    funlockfile(stderr);

    for (unsigned i = 0; i < 8; ++i) {
        void *temporary = malloc(48);
        assert(temporary);
        memset(temporary, (int)i, 48);
        assert(printf("WORKER LIBC startup row=%u text=clear\n", i) > 0);
        free(temporary);
    }
    assert(knytos_thread_join(1) == KNYTOS_OK && atomic_load(&s.complete) == 1);
    assert(knytos_thread_start(1, 1, different_generation, &s) == KNYTOS_OK);
    assert(knytos_thread_join(1) == KNYTOS_OK && atomic_load(&s.reset) == 1);
    assert(errno == EDOM);
    assert(knytos_tsd_get(s.key) == &s.entered);
    assert(knytos_tsd_set(s.key, NULL) == KNYTOS_OK);
    assert(knytos_tsd_get(s.key) == NULL);
    assert(knytos_tsd_key_delete(s.key) == KNYTOS_OK);
    assert(knytos_tsd_key_delete(s.key) == KNYTOS_INVALID);
    assert(knytos_tsd_key_delete(replacement) == KNYTOS_OK);
    uint64_t keys[16];
    for (unsigned i = 0; i < 16; ++i)
        assert(knytos_tsd_key_create(&keys[i]) == KNYTOS_OK);
    uint64_t unchanged = UINT64_MAX;
    assert(knytos_tsd_key_create(&unchanged) == KNYTOS_UNAVAILABLE);
    assert(unchanged == UINT64_MAX);
    for (unsigned i = 0; i < 16; ++i)
        assert(knytos_tsd_key_delete(keys[i]) == KNYTOS_OK);
    puts("WORKER LIBC TSD isolation deletion reuse and admission");
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("WORKER LIBC PASS");
    return 0;
}
