/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ATOMVM_SERVICE_WORKER_H
#define KNYTOS_ATOMVM_SERVICE_WORKER_H

#include "globalcontext.h"

extern const struct Nif knytos_request_nif;
extern const struct Nif knytos_cancel_nif;
void knytos_service_worker_start(void);
void knytos_service_worker_join(void);
bool knytos_service_reply_ready(void);
void knytos_service_deliver(GlobalContext *global);

#endif
