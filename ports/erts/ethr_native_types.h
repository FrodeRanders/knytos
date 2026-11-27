/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_NATIVE_TYPES_H
#define KNYTOS_ETHR_NATIVE_TYPES_H

#include <stdint.h>

/* Non-owning runtime identity. The adapter retains the native worker owner. */
typedef struct {
    uint32_t slot;
    uint64_t generation;
} ethr_tid;

/* Runtime-local key identity. Values remain borrowed by the participant. */
typedef uint64_t ethr_tsd_key;

#endif
