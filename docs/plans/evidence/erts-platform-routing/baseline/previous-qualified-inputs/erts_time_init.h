/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ERTS_TIME_INIT_H
#define KNYTOS_ERTS_TIME_INIT_H

#ifdef KNYTOS_ERTS_TIME_CONTRACT_PROBE
/* Generated from the pinned common sys.h. This explicit profile checks its
 * time record/prototypes, not the complete ERTS platform header. */
#include "erts_time_contract.h"
#else
#include "sys.h"
#endif
#include "erts_clock_platform.h"

/* Status-bearing preparation: query and validation precede publication;
 * output changes only on success. No runtime refresh producer is installed.
 * Call before time-supervisor locks/workers, with a nonzero sample-age policy.
 * The copied metadata describes granularity, never UTC accuracy/security. */
int knytos_erts_init_time(ErtsSysInitTimeResult *output, uint64_t max_elapsed_ns);

#endif
