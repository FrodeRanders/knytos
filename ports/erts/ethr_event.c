/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "ethr_event.h"

static void require_ok(int status)
{
    if (status != KNYTOS_OK) {
        static const char message[] = "[erts] event operation failed\n";
        knytos_console_write(message, sizeof(message) - 1);
        abort();
    }
}

int ethr_event_init(ethr_event *event)
{
    if (!event) return EINVAL;
    event->native = knytos_event_create();
    return event->native ? 0 : ENOMEM;
}

int ethr_event_prepare_timed(ethr_event *event)
{
    /* The native checked wait supplies its own timer admission. */
    return event && event->native ? 0 : EINVAL;
}

int ethr_event_destroy(ethr_event *event)
{
    if (!event) return EINVAL;
    int status = knytos_event_destroy(event->native);
    if (status == KNYTOS_INVALID) return EINVAL;
    if (status == KNYTOS_BUSY) return EBUSY;
    require_ok(status);
    event->native = NULL;
    return 0;
}

void ethr_event_set(ethr_event *event)
{
    require_ok(knytos_event_set(event ? event->native : NULL));
    atomic_thread_fence(memory_order_seq_cst);
}

void ethr_event_reset(ethr_event *event)
{
    require_ok(knytos_event_reset(event ? event->native : NULL));
    atomic_thread_fence(memory_order_seq_cst);
}

int ethr_event_stwait(ethr_event *event, int spincount, int64_t timeout_ns)
{
    if (!event || !event->native || spincount < 0) {
        require_ok(KNYTOS_INVALID);
    }
    /* The spin count is optional work before parking. Go directly to the
     * registered native wait, retaining the same timeout budget. */
    int64_t milliseconds = -1;
    if (timeout_ns >= 0) {
        milliseconds = timeout_ns / 1000000 + (timeout_ns % 1000000 != 0);
    }
    int status = knytos_event_wait_on(event->native, milliseconds);
    if (status == KNYTOS_DEADLINE) return ETIMEDOUT;
    require_ok(status);
    atomic_thread_fence(memory_order_acquire);
    return 0;
}

int ethr_event_wait(ethr_event *event)
{
    return ethr_event_stwait(event, 0, -1);
}

int ethr_event_swait(ethr_event *event, int spincount)
{
    return ethr_event_stwait(event, spincount, -1);
}

int ethr_event_twait(ethr_event *event, int64_t timeout_ns)
{
    return ethr_event_stwait(event, 0, timeout_ns);
}
