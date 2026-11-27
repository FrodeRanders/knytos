/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_EXTENSIONS_H
#define KNYTOS_ETHR_EXTENSIONS_H

/* Private bounded-profile bootstrap, never a substitute for ethr_init or
 * ethr_late_init. Include after OTP's selected header, or use standalone. */
#if defined(ETHREAD_H__)
#if !defined(ETHR_KNYTOS)
#error "Knytos extensions require the Knytos thread backend"
#endif
#else
#include "knytos_ethread.h"
#endif

#define KNYTOS_ETHR_TS_EVENT_LIMIT 16
#define KNYTOS_ETHR_MAIN_THREAD_LIMIT 4

int knytos_ethr_threads_init(const ethr_init_data *data);
int knytos_ethr_threads_shutdown(void);
int knytos_ethr_ts_events_init(void);
int knytos_ethr_ts_events_init_opt(const ethr_late_init_data *data);
int knytos_ethr_ts_events_clear(void);

#endif
