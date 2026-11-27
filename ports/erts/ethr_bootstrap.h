/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_BOOTSTRAP_H
#define KNYTOS_ETHR_BOOTSTRAP_H

#include "ethr_ts_event.h"

/* Internal early/late split. Only the startup thread's bootstrap transaction
 * reserves the key and commits the first cached event/configuration. */
int knytos_ethr_ts_events_reserve(void);
int knytos_ethr_ts_events_start(const ethr_late_init_data *data);

#endif
