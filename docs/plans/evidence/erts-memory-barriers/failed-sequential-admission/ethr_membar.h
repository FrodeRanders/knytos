/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_MEMBAR_H
#define KNYTOS_ETHR_MEMBAR_H

/* The pinned OTP GCC backend's four ordering directions. */
#define ETHR_LoadLoad (1 << 0)
#define ETHR_LoadStore (1 << 1)
#define ETHR_StoreLoad (1 << 2)
#define ETHR_StoreStore (1 << 3)

#define ETHR_COMPILER_BARRIER __asm__ __volatile__("" : : : "memory")

/* OTP requires ordering around this particular site, including accesses
 * outside the C11 atomic API. Use the legacy builtin's full-barrier contract
 * rather than inferring that contract from a C11 memory-order argument.
 * Every mask, including zero, gets a full barrier. Evaluate it once and
 * retain the expression form used by the upstream API. */
#define ETHR_MEMBAR(B) ((void)(B), __sync_synchronize())
#define ETHR_MEMORY_BARRIER \
    ETHR_MEMBAR(ETHR_LoadLoad | ETHR_LoadStore | ETHR_StoreLoad | ETHR_StoreStore)
#define ETHR_WRITE_MEMORY_BARRIER ETHR_MEMBAR(ETHR_StoreStore)
#define ETHR_READ_MEMORY_BARRIER ETHR_MEMBAR(ETHR_LoadLoad)
#define ETHR_READ_DEPEND_MEMORY_BARRIER ETHR_MEMBAR(ETHR_LoadLoad)

/* No ETHR_ORDERED_READ_DEPEND shortcut or instruction-cache/JIT barrier is
 * advertised. Data ordering alone does not synchronize instruction fetch. */

#endif
