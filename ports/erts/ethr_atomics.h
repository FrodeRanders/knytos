/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_ATOMICS_H
#define KNYTOS_ETHR_ATOMICS_H

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

typedef int32_t ethr_sint32_t;
typedef uint32_t ethr_uint32_t;
typedef int64_t ethr_sint64_t;
typedef uint64_t ethr_uint64_t;
typedef intptr_t ethr_sint_t;
typedef uintptr_t ethr_uint_t;

typedef struct { _Atomic(ethr_uint32_t) value; } ethr_atomic32_t;
typedef struct { _Atomic(ethr_uint_t) value; } ethr_atomic_t;

_Static_assert(sizeof(ethr_sint_t) == 8, "This profile requires a 64-bit word");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2,
               "This profile requires native 32-bit and word atomic operations");
_Static_assert(sizeof(ethr_atomic32_t) == sizeof(ethr_sint32_t), "Atomic32 address layout");
_Static_assert(sizeof(ethr_atomic_t) == sizeof(ethr_sint_t), "Atomic word address layout");

/* All variants deliberately use full fences, including the unspecified and
 * dependency-only variants. A seq_cst load/store alone does not express
 * OTP's two-sided full-barrier contract. Initialization is exclusive. */
static inline void knytos_ethr_atomic_fence(void)
{
    atomic_thread_fence(memory_order_seq_cst);
}

#define KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, B) \
static inline void P##_init##B(P##_t *var, S value) \
{ \
    knytos_ethr_atomic_fence(); \
    atomic_init(&var->value, (U)value); \
    knytos_ethr_atomic_fence(); \
} \
static inline void P##_set##B(P##_t *var, S value) \
{ \
    knytos_ethr_atomic_fence(); \
    atomic_store_explicit(&var->value, (U)value, memory_order_seq_cst); \
    knytos_ethr_atomic_fence(); \
} \
static inline S P##_read##B(P##_t *var) \
{ \
    knytos_ethr_atomic_fence(); \
    U value = atomic_load_explicit(&var->value, memory_order_seq_cst); \
    knytos_ethr_atomic_fence(); \
    return knytos_##P##_from_bits(value); \
} \
static inline S P##_xchg##B(P##_t *var, S value) \
{ \
    knytos_ethr_atomic_fence(); \
    U old = atomic_exchange_explicit(&var->value, (U)value, memory_order_seq_cst); \
    knytos_ethr_atomic_fence(); \
    return knytos_##P##_from_bits(old); \
} \
static inline S P##_cmpxchg##B(P##_t *var, S value, S old_value) \
{ \
    U observed = (U)old_value; \
    knytos_ethr_atomic_fence(); \
    atomic_compare_exchange_strong_explicit(&var->value, &observed, (U)value, \
                                             memory_order_seq_cst, memory_order_seq_cst); \
    knytos_ethr_atomic_fence(); \
    return knytos_##P##_from_bits(observed); \
} \
static inline S P##_add_read##B(P##_t *var, S value) \
{ \
    knytos_ethr_atomic_fence(); \
    U old = atomic_fetch_add_explicit(&var->value, (U)value, memory_order_seq_cst); \
    knytos_ethr_atomic_fence(); \
    return knytos_##P##_from_bits((U)(old + (U)value)); \
} \
static inline S P##_inc_read##B(P##_t *var) { return P##_add_read##B(var, 1); } \
static inline S P##_dec_read##B(P##_t *var) { return P##_add_read##B(var, -1); } \
static inline void P##_add##B(P##_t *var, S value) { (void)P##_add_read##B(var, value); } \
static inline void P##_inc##B(P##_t *var) { (void)P##_inc_read##B(var); } \
static inline void P##_dec##B(P##_t *var) { (void)P##_dec_read##B(var); } \
static inline S P##_read_band##B(P##_t *var, S value) \
{ \
    knytos_ethr_atomic_fence(); \
    U old = atomic_fetch_and_explicit(&var->value, (U)value, memory_order_seq_cst); \
    knytos_ethr_atomic_fence(); \
    return knytos_##P##_from_bits(old); \
} \
static inline S P##_read_bor##B(P##_t *var, S value) \
{ \
    knytos_ethr_atomic_fence(); \
    U old = atomic_fetch_or_explicit(&var->value, (U)value, memory_order_seq_cst); \
    knytos_ethr_atomic_fence(); \
    return knytos_##P##_from_bits(old); \
}

/* Preserve signed bit patterns while doing arithmetic modulo the width.
 * No signed expression computes the potentially overflowing new value. */
#define KNYTOS_ETHR_ATOMIC_FAMILY(P, S, U) \
static inline S knytos_##P##_from_bits(U bits) \
{ \
    S value; \
    memcpy(&value, &bits, sizeof(value)); \
    return value; \
} \
static inline S *P##_addr(P##_t *var) \
{ \
    return (S *)(void *)&var->value; \
} \
KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, ) \
KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, _ddrb) \
KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, _rb) \
KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, _wb) \
KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, _acqb) \
KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, _relb) \
KNYTOS_ETHR_ATOMIC_VARIANT(P, S, U, _mb)

/* addr is a borrowed interoperability address. Ordinary accesses through it
 * require external exclusion from atomic users. It owns no storage. */
KNYTOS_ETHR_ATOMIC_FAMILY(ethr_atomic32, ethr_sint32_t, ethr_uint32_t)
KNYTOS_ETHR_ATOMIC_FAMILY(ethr_atomic, ethr_sint_t, ethr_uint_t)

#undef KNYTOS_ETHR_ATOMIC_FAMILY
#undef KNYTOS_ETHR_ATOMIC_VARIANT

#endif
