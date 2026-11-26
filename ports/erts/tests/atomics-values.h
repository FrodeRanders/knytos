/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_TEST_ATOMICS_VALUES_H
#define KNYTOS_TEST_ATOMICS_VALUES_H

#include <assert.h>
#include "ethr_atomics.h"

#define CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, B) do { \
    P##_t value; \
    P##_init##B(&value, 17); \
    assert(P##_read##B(&value) == 17); \
    P##_set##B(&value, -5); \
    assert(P##_read##B(&value) == -5); \
    assert(P##_xchg##B(&value, 11) == -5); \
    assert(P##_cmpxchg##B(&value, 19, 9) == 11); \
    assert(P##_read##B(&value) == 11); \
    assert(P##_cmpxchg##B(&value, 19, 11) == 11); \
    assert(P##_read##B(&value) == 19); \
    assert(P##_add_read##B(&value, -3) == 16); \
    assert(P##_inc_read##B(&value) == 17); \
    assert(P##_dec_read##B(&value) == 16); \
    P##_add##B(&value, 4); \
    P##_inc##B(&value); \
    P##_dec##B(&value); \
    assert(P##_read_band##B(&value, 15) == 20); \
    assert(P##_read##B(&value) == 4); \
    assert(P##_read_bor##B(&value, 8) == 4); \
    assert(P##_read##B(&value) == 12); \
    P##_set##B(&value, MAXIMUM); \
    assert(P##_inc_read##B(&value) == MINIMUM); \
    assert(P##_dec_read##B(&value) == MAXIMUM); \
    P##_set##B(&value, MINIMUM); \
    assert(P##_add_read##B(&value, -1) == MAXIMUM); \
    assert((void *)P##_addr(&value) == (void *)&value); \
    assert((uintptr_t)P##_addr(&value) % _Alignof(S) == 0); \
} while (0)

#define CHECK_FAMILY(P, S, MAXIMUM, MINIMUM) do { \
    CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, ); \
    CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, _ddrb); \
    CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, _rb); \
    CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, _wb); \
    CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, _acqb); \
    CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, _relb); \
    CHECK_VARIANT(P, S, MAXIMUM, MINIMUM, _mb); \
} while (0)

static inline void check_atomic_values(void)
{
    CHECK_FAMILY(ethr_atomic32, ethr_sint32_t, INT32_MAX, INT32_MIN);
    CHECK_FAMILY(ethr_atomic, ethr_sint_t, INTPTR_MAX, INTPTR_MIN);
}

#undef CHECK_FAMILY
#undef CHECK_VARIANT

#endif
