/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_DW_ATOMICS_H
#define KNYTOS_ETHR_DW_ATOMICS_H

#ifndef KNYTOS_ETHR_ATOMICS_H
#error "Include ethr_atomics.h before this implementation header"
#endif

/* Native builtins access two adjacent word objects as one value. may_alias
 * is the compiler extension permitting that access without type-based alias
 * assumptions about the caller's word storage. No kernel resource is owned. */
__extension__ typedef unsigned __int128 __attribute__((__may_alias__)) knytos_ethr_dw_bits;

typedef union {
    ethr_sint_t sint[2];
} ethr_dw_sint_t;

/* Preserve OTP's one-word base-alignment requirement. The selected pair is
 * 16-byte aligned inside these three words. An initialized object must stay
 * at its original address through all users; copying/moving it is not valid. */
typedef struct {
    ethr_sint_t storage[3];
} ethr_dw_atomic_t;
typedef ethr_dw_atomic_t ethr_native_dw_atomic_t;

_Static_assert(sizeof(knytos_ethr_dw_bits) == 16, "Double-word width");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "This profile is little endian");
_Static_assert(__atomic_always_lock_free(16, 0), "Native lock-free double-word operations required");
_Static_assert(sizeof(ethr_dw_sint_t) == 16, "Double-word value layout");
_Static_assert(sizeof(ethr_dw_atomic_t) == 24 && _Alignof(ethr_dw_atomic_t) == 8,
               "Double-word storage accepts word-aligned objects");

#define ETHR_DW_SINT_LOW_WORD 0
#define ETHR_DW_SINT_HIGH_WORD 1
#define ETHR_HAVE_DOUBLE_WORD_SZ_NATIVE_ATOMIC_OPS 1
#define ETHR_HAVE_NATIVE_DW_ATOMIC 1
#define ETHR_NATIVE_DW_ATOMIC_IMPL "knytos_atomic128_full_fences"
#define ETHR_HAVE_ETHR_NATIVE_DW_ATOMIC_ADDR 1
#define ETHR_HAVE_ETHR_NATIVE_DW_ATOMIC_INIT_MB 1
#define ETHR_HAVE_ETHR_NATIVE_DW_ATOMIC_SET_MB 1
#define ETHR_HAVE_ETHR_NATIVE_DW_ATOMIC_READ_MB 1
#define ETHR_HAVE_ETHR_NATIVE_DW_ATOMIC_CMPXCHG_MB 1

static inline ethr_sint_t *ethr_native_dw_atomic_addr(ethr_native_dw_atomic_t *var)
{
    /* A valid object's base is aligned to eight, hence its low four bits
     * are either zero or eight. Both selected words remain inside storage. */
    return &var->storage[((uintptr_t)&var->storage[0] & 15) != 0];
}

static inline ethr_sint_t *ethr_dw_atomic_addr(ethr_dw_atomic_t *var)
{
    return ethr_native_dw_atomic_addr(var);
}

static inline int ethr_have_native_dw_atomic(void)
{
    return 1; /* The compile-time check above excludes a library/locked fallback. */
}

static inline knytos_ethr_dw_bits knytos_ethr_dw_pack(const ethr_sint_t *value)
{
    knytos_ethr_dw_bits bits;
    memcpy(&bits, value, sizeof(bits));
    return bits;
}

static inline void knytos_ethr_dw_unpack(ethr_sint_t *value, knytos_ethr_dw_bits bits)
{
    memcpy(value, &bits, sizeof(bits));
}

/* Native word-pair signatures also drive the public value-union API. */
static __inline__ __attribute__((__always_inline__)) void
ethr_native_dw_atomic_init_mb(ethr_native_dw_atomic_t *var, ethr_sint_t *value)
{
    knytos_ethr_dw_bits next = knytos_ethr_dw_pack(value);
    knytos_ethr_full_fence();
    memcpy(ethr_native_dw_atomic_addr(var), &next, sizeof(next));
    knytos_ethr_full_fence();
}

static __inline__ __attribute__((__always_inline__)) void
ethr_native_dw_atomic_set_mb(ethr_native_dw_atomic_t *var, ethr_sint_t *value)
{
    knytos_ethr_dw_bits next = knytos_ethr_dw_pack(value);
    knytos_ethr_dw_bits *address = (knytos_ethr_dw_bits *)(void *)ethr_native_dw_atomic_addr(var);
    knytos_ethr_full_fence();
    __atomic_store_n(address, next, __ATOMIC_SEQ_CST);
    knytos_ethr_full_fence();
}

static __inline__ __attribute__((__always_inline__)) void
ethr_native_dw_atomic_read_mb(ethr_native_dw_atomic_t *var, ethr_sint_t *value)
{
    knytos_ethr_dw_bits *address = (knytos_ethr_dw_bits *)(void *)ethr_native_dw_atomic_addr(var);
    knytos_ethr_full_fence();
    knytos_ethr_dw_bits bits = __atomic_load_n(address, __ATOMIC_SEQ_CST);
    knytos_ethr_full_fence();
    knytos_ethr_dw_unpack(value, bits);
}

static __inline__ __attribute__((__always_inline__)) int
ethr_native_dw_atomic_cmpxchg_mb(ethr_native_dw_atomic_t *var, ethr_sint_t *value,
                               ethr_sint_t *old_value)
{
    knytos_ethr_dw_bits next = knytos_ethr_dw_pack(value);
    knytos_ethr_dw_bits observed = knytos_ethr_dw_pack(old_value);
    knytos_ethr_dw_bits *address = (knytos_ethr_dw_bits *)(void *)ethr_native_dw_atomic_addr(var);
    knytos_ethr_full_fence();
    int success = __atomic_compare_exchange_n(address, &observed, next, 0,
                                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    knytos_ethr_full_fence();
    if (!success) knytos_ethr_dw_unpack(old_value, observed);
    return success;
}

/* Each public variant retains the native full-fence primitive. Value
 * arguments are borrowed, separate storage, retained throughout the call. */
#define KNYTOS_ETHR_DW_VARIANT(B) \
static inline void ethr_dw_atomic_init##B(ethr_dw_atomic_t *var, ethr_dw_sint_t *value) \
{ ethr_native_dw_atomic_init_mb(var, value->sint); } \
static inline void ethr_dw_atomic_set##B(ethr_dw_atomic_t *var, ethr_dw_sint_t *value) \
{ ethr_native_dw_atomic_set_mb(var, value->sint); } \
static inline void ethr_dw_atomic_read##B(ethr_dw_atomic_t *var, ethr_dw_sint_t *value) \
{ ethr_native_dw_atomic_read_mb(var, value->sint); } \
static inline int ethr_dw_atomic_cmpxchg##B(ethr_dw_atomic_t *var, ethr_dw_sint_t *value, \
                                         ethr_dw_sint_t *old_value) \
{ \
    return ethr_native_dw_atomic_cmpxchg_mb(var, value->sint, old_value->sint); \
}

KNYTOS_ETHR_DW_VARIANT()
KNYTOS_ETHR_DW_VARIANT(_ddrb)
KNYTOS_ETHR_DW_VARIANT(_rb)
KNYTOS_ETHR_DW_VARIANT(_wb)
KNYTOS_ETHR_DW_VARIANT(_acqb)
KNYTOS_ETHR_DW_VARIANT(_relb)
KNYTOS_ETHR_DW_VARIANT(_mb)
#undef KNYTOS_ETHR_DW_VARIANT

#endif
