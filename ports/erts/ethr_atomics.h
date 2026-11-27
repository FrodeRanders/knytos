/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_ATOMICS_H
#define KNYTOS_ETHR_ATOMICS_H

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "ethr_membar.h"

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

/* Source-level native primitive aliases used by OTP introspection. Only
 * full-barrier primitives are advertised; weaker public forms retain
 * their existing stronger implementation. These are borrowed addresses. */
typedef ethr_atomic32_t ethr_native_atomic32_t;
typedef ethr_atomic_t ethr_native_atomic64_t;
_Static_assert(__atomic_always_lock_free(4, 0) && __atomic_always_lock_free(8, 0),
               "Native scalar primitives required");
#define ETHR_HAVE_NATIVE_ATOMIC32 1
#define ETHR_HAVE_NATIVE_ATOMIC64 1
#define ETHR_HAVE_32BIT_NATIVE_ATOMIC_OPS 1
#define ETHR_HAVE_WORD_SZ_NATIVE_ATOMIC_OPS 1
#define ETHR_NATIVE_ATOMIC32_IMPL "knytos_c11_full_fences"
#define ETHR_NATIVE_ATOMIC64_IMPL "knytos_c11_full_fences"
#define ethr_native_atomic32_addr ethr_atomic32_addr
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_ADDR 1
#define ethr_native_atomic32_cmpxchg_mb ethr_atomic32_cmpxchg_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_CMPXCHG_MB 1
#define ethr_native_atomic32_xchg_mb ethr_atomic32_xchg_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_XCHG_MB 1
#define ethr_native_atomic32_set_mb ethr_atomic32_set_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_SET_MB 1
#define ethr_native_atomic32_init_mb ethr_atomic32_init_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_INIT_MB 1
#define ethr_native_atomic32_add_return_mb ethr_atomic32_add_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_ADD_RETURN_MB 1
#define ethr_native_atomic32_read_mb ethr_atomic32_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_READ_MB 1
#define ethr_native_atomic32_inc_return_mb ethr_atomic32_inc_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_INC_RETURN_MB 1
#define ethr_native_atomic32_dec_return_mb ethr_atomic32_dec_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_DEC_RETURN_MB 1
#define ethr_native_atomic32_add_mb ethr_atomic32_add_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_ADD_MB 1
#define ethr_native_atomic32_inc_mb ethr_atomic32_inc_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_INC_MB 1
#define ethr_native_atomic32_dec_mb ethr_atomic32_dec_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_DEC_MB 1
#define ethr_native_atomic32_and_retold_mb ethr_atomic32_read_band_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_AND_RETOLD_MB 1
#define ethr_native_atomic32_or_retold_mb ethr_atomic32_read_bor_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC32_OR_RETOLD_MB 1
#define ethr_native_atomic64_addr ethr_atomic_addr
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_ADDR 1
#define ethr_native_atomic64_cmpxchg_mb ethr_atomic_cmpxchg_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_CMPXCHG_MB 1
#define ethr_native_atomic64_xchg_mb ethr_atomic_xchg_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_XCHG_MB 1
#define ethr_native_atomic64_set_mb ethr_atomic_set_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_SET_MB 1
#define ethr_native_atomic64_init_mb ethr_atomic_init_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_INIT_MB 1
#define ethr_native_atomic64_add_return_mb ethr_atomic_add_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_ADD_RETURN_MB 1
#define ethr_native_atomic64_read_mb ethr_atomic_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_READ_MB 1
#define ethr_native_atomic64_inc_return_mb ethr_atomic_inc_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_INC_RETURN_MB 1
#define ethr_native_atomic64_dec_return_mb ethr_atomic_dec_read_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_DEC_RETURN_MB 1
#define ethr_native_atomic64_add_mb ethr_atomic_add_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_ADD_MB 1
#define ethr_native_atomic64_inc_mb ethr_atomic_inc_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_INC_MB 1
#define ethr_native_atomic64_dec_mb ethr_atomic_dec_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_DEC_MB 1
#define ethr_native_atomic64_and_retold_mb ethr_atomic_read_band_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_AND_RETOLD_MB 1
#define ethr_native_atomic64_or_retold_mb ethr_atomic_read_bor_mb
#define ETHR_HAVE_ETHR_NATIVE_ATOMIC64_OR_RETOLD_MB 1

#include "ethr_dw_atomics.h"

/* Stateless native-only setup; this is not ethr_init/ethr_late_init. */
int ethr_init_atomics(void);
/* Legacy mutable signatures return borrowed immutable tables/strings.
 * Callers must not modify or free them. */
char **ethr_native_atomic32_ops(void);
char **ethr_native_atomic64_ops(void);
char **ethr_native_dw_atomic_ops(void);
char **ethr_native_su_dw_atomic_ops(void);

#endif
