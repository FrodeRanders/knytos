#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "knytos.h"

struct shared {
    struct knytos_rwlock *lock;
    atomic_uint ready;
    atomic_uint read_result;
    atomic_uint write_result;
    unsigned payload;
};

static void announce(atomic_uint *flag, unsigned result)
{
    atomic_store_explicit(flag, result, memory_order_release);
    knytos_thread_signal(0);
}

static unsigned await(atomic_uint *flag, uint32_t timeout_ms)
{
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    const uint64_t until = now + (uint64_t)timeout_ms * 1000000;
    for (;;) {
        unsigned value = atomic_load_explicit(flag, memory_order_acquire);
        if (value)
            return value;
        assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
        assert(now < until);
        int status = knytos_thread_wait((uint32_t)((until - now + 999999) / 1000000));
        assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
    }
}

/* The fixture has no observer for a private lock object's registration.
 * Give the submitted writer a scheduling opportunity, then independently
 * require a fresh reader to observe its preference. */
static void let_writer_park(void)
{
    uint64_t now;
    assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    const uint64_t until = now + 40000000;
    do {
        int status = knytos_event_wait((int64_t)((until - now + 999999) / 1000000));
        assert(status == KNYTOS_OK || status == KNYTOS_DEADLINE);
        assert(knytos_clock_monotonic_ns(&now) == KNYTOS_OK);
    } while (now < until);
}

static void shared_reader(void *argument)
{
    struct shared *s = argument;
    int valid = knytos_rwlock_unlock(s->lock) == KNYTOS_INVALID
        && knytos_rwlock_try_read(s->lock) == KNYTOS_OK
        && knytos_rwlock_read(s->lock, 0) == KNYTOS_OK
        && s->payload == 7
        && knytos_rwlock_write(s->lock, 0) == KNYTOS_INVALID
        && knytos_rwlock_unlock(s->lock) == KNYTOS_OK
        && knytos_rwlock_unlock(s->lock) == KNYTOS_OK
        && knytos_rwlock_unlock(s->lock) == KNYTOS_INVALID;
    announce(&s->read_result, valid ? 1 : 2);
}

static void timed_writer(void *argument)
{
    struct shared *s = argument;
    announce(&s->ready, 1);
    int status = knytos_rwlock_write(s->lock, 450);
    announce(&s->write_result, status == KNYTOS_DEADLINE ? 1 : 2);
}

static void queued_reader(void *argument)
{
    struct shared *s = argument;
    int valid = knytos_rwlock_try_read(s->lock) == KNYTOS_BUSY
        && knytos_rwlock_read(s->lock, 3000) == KNYTOS_OK
        && s->payload == 7
        && knytos_rwlock_unlock(s->lock) == KNYTOS_OK;
    announce(&s->read_result, valid ? 1 : 2);
}

static void exclusive_writer(void *argument)
{
    struct shared *s = argument;
    if (knytos_rwlock_write(s->lock, 0) != KNYTOS_DEADLINE) {
        announce(&s->write_result, 2);
        return;
    }
    announce(&s->ready, 1);
    if (knytos_rwlock_write(s->lock, 2000) != KNYTOS_OK) {
        announce(&s->write_result, 2);
        return;
    }
    int valid = knytos_rwlock_try_read(s->lock) == KNYTOS_INVALID
        && knytos_rwlock_write(s->lock, 0) == KNYTOS_INVALID;
    s->payload = 19;
    valid = knytos_rwlock_unlock(s->lock) == KNYTOS_OK && valid;
    announce(&s->write_result, valid ? 1 : 2);
}

int main(void)
{
    struct shared s = {.lock = knytos_rwlock_create(), .payload = 7};
    assert(s.lock);
    assert(knytos_rwlock_read(s.lock, 0) == KNYTOS_OK);
    assert(knytos_rwlock_destroy(s.lock) == KNYTOS_BUSY);
    assert(knytos_thread_start(1, 1, shared_reader, &s) == KNYTOS_OK);
    assert(await(&s.read_result, 1000) == 1);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    puts("RWLOCK shared and nested readers");

    atomic_store(&s.read_result, 0);
    assert(knytos_thread_start(1, 1, timed_writer, &s) == KNYTOS_OK);
    assert(await(&s.ready, 1000) == 1);
    let_writer_park();
    assert(knytos_thread_start(2, 1, queued_reader, &s) == KNYTOS_OK);
    /* Keep the first reader active. Only the timed writer's departure can
     * reopen admission before the other reader's own deadline. */
    assert(await(&s.read_result, 1600) == 1);
    assert(await(&s.write_result, 1000) == 1);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(knytos_thread_join(2) == KNYTOS_OK);
    puts("RWLOCK writer timeout wakes readers");

    atomic_store(&s.ready, 0);
    atomic_store(&s.write_result, 0);
    assert(knytos_thread_start(1, 1, exclusive_writer, &s) == KNYTOS_OK);
    assert(await(&s.ready, 1000) == 1);
    assert(knytos_rwlock_try_read(s.lock) == KNYTOS_OK);
    assert(knytos_rwlock_unlock(s.lock) == KNYTOS_OK);
    assert(atomic_load(&s.write_result) == 0);
    assert(knytos_rwlock_unlock(s.lock) == KNYTOS_OK);
    assert(await(&s.write_result, 1000) == 1);
    assert(knytos_thread_join(1) == KNYTOS_OK);
    assert(knytos_rwlock_try_read(s.lock) == KNYTOS_OK);
    assert(s.payload == 19);
    assert(knytos_rwlock_unlock(s.lock) == KNYTOS_OK);
    assert(knytos_rwlock_destroy(s.lock) == KNYTOS_OK);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    puts("RWLOCK last reader releases writer");
    puts("RWLOCK PASS");
    return 0;
}
