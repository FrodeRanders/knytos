/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_EVENT_H
#define KNYTOS_ETHR_EVENT_H

#include <stdint.h>
#include "knytos.h"

/* Event portion of the pinned OTP 29.1.2 ethread interface. The enclosing
 * ERTS thread backend must retain this object through producer and waiter
 * calls and arrange a terminal set before joining an indefinite waiter. */
typedef struct {
    struct knytos_event *native;
} ethr_event;

int ethr_event_init(ethr_event *event);
int ethr_event_prepare_timed(ethr_event *event);
int ethr_event_destroy(ethr_event *event);
void ethr_event_set(ethr_event *event);
void ethr_event_reset(ethr_event *event);
int ethr_event_wait(ethr_event *event);
int ethr_event_swait(ethr_event *event, int spincount);
int ethr_event_twait(ethr_event *event, int64_t timeout_ns);
int ethr_event_stwait(ethr_event *event, int spincount, int64_t timeout_ns);

#endif
