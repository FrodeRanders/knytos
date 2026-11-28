/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ERTS_CALENDAR_H
#define KNYTOS_ERTS_CALENDAR_H

#include <time.h>
#include "erts_system_time.h"

/* Initial headless profile: local time is UTC, without DST or TZ lookup.
 * Reads use the published snapshot, never a service query. Errors preserve
 * output. Keep a valid, aligned output object borrowed through the call. */
int knytos_erts_calendar_now(struct tm *output);
void knytos_erts_calendar_now_or_abort(struct tm *output);

#endif
