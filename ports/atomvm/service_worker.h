/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ATOMVM_SERVICE_WORKER_H
#define KNYTOS_ATOMVM_SERVICE_WORKER_H

#include "globalcontext.h"

extern const struct Nif knytos_request_nif;
extern const struct Nif knytos_cancel_nif;
extern const struct Nif knytos_lookup_nif;
extern const struct Nif knytos_close_nif;
extern const struct Nif knytos_subscribe_nif;
extern const struct Nif knytos_unsubscribe_nif;
void knytos_service_worker_start(GlobalContext *global);
void knytos_service_worker_join(void);
void knytos_service_owner_terminated(Context *owner);
void knytos_service_flush_cleanup(void);
bool knytos_service_needs_scan(void);
bool knytos_service_deliver(GlobalContext *global);

#endif
