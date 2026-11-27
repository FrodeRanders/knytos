/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_TEST_DW_ATOMICS_VALUES_H
#define KNYTOS_TEST_DW_ATOMICS_VALUES_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    void (*init)(ethr_dw_atomic_t *, ethr_dw_sint_t *);
    void (*set)(ethr_dw_atomic_t *, ethr_dw_sint_t *);
    void (*read)(ethr_dw_atomic_t *, ethr_dw_sint_t *);
    int (*cas)(ethr_dw_atomic_t *, ethr_dw_sint_t *, ethr_dw_sint_t *);
} dw_operations;
#define DW_OPS(B) {ethr_dw_atomic_init##B, ethr_dw_atomic_set##B, \
                   ethr_dw_atomic_read##B, ethr_dw_atomic_cmpxchg##B}
static const dw_operations dw_variants[] = {
    DW_OPS(), DW_OPS(_ddrb), DW_OPS(_rb), DW_OPS(_wb),
    DW_OPS(_acqb), DW_OPS(_relb), DW_OPS(_mb)
};
#undef DW_OPS

static int dw_equal(ethr_dw_sint_t first, ethr_dw_sint_t second)
{
    return first.sint[0] == second.sint[0] && first.sint[1] == second.sint[1];
}

static void check_dw_values(void)
{
    struct {
        uint64_t before;
        _Alignas(16) ethr_dw_atomic_t objects[2];
        uint64_t after;
    } guarded;
    assert(ethr_have_native_dw_atomic() == 1);
    assert(ETHR_DW_SINT_LOW_WORD == 0 && ETHR_DW_SINT_HIGH_WORD == 1);
    for (unsigned variant = 0; variant < sizeof(dw_variants) / sizeof(dw_variants[0]); ++variant) {
        const dw_operations *op = &dw_variants[variant];
        guarded.before = UINT64_C(0x123456789abcdef0);
        guarded.after = UINT64_C(0xfedcba9876543210);
        memset(guarded.objects, 0xa5, sizeof(guarded.objects));
        for (unsigned alignment = 0; alignment < 2; ++alignment) {
            ethr_dw_atomic_t *object = &guarded.objects[alignment];
            assert(((uintptr_t)object & 15) == alignment * 8);
            ethr_sint_t *address = ethr_dw_atomic_addr(object);
            assert(((uintptr_t)address & 15) == 0);
            assert(address >= &object->storage[0] && address + 2 <= &object->storage[3]);
            ethr_dw_sint_t first = {.sint = {INTPTR_MIN, INTPTR_MAX}};
            ethr_dw_sint_t second = {.sint = {INTPTR_MAX, INTPTR_MIN}};
            ethr_dw_sint_t observed, expected, snapshot;
            op->init(object, &first);
            op->read(object, &observed);
            assert(dw_equal(first, observed));
            op->set(object, &second);
            /* Mismatch independently in each half; both must be compared. */
            for (unsigned half = 0; half < 2; ++half) {
                expected = second;
                expected.sint[half] = 42;
                assert(op->cas(object, &first, &expected) == 0 && dw_equal(expected, second));
                op->read(object, &observed);
                assert(dw_equal(observed, second));
            }
            expected = second;
            snapshot = expected;
            assert(op->cas(object, &first, &expected) == 1 && dw_equal(expected, snapshot));
            assert(dw_equal(first, (ethr_dw_sint_t){.sint = {INTPTR_MIN, INTPTR_MAX}}));
            op->read(object, &observed);
            assert(dw_equal(first, observed));
            /* Value and expected may be the same separate value object. */
            expected = first;
            assert(op->cas(object, &expected, &expected) == 1 && dw_equal(expected, first));
            expected = second;
            assert(op->cas(object, &expected, &expected) == 0 && dw_equal(expected, first));
            /* Exclusive borrowed-address interoperability uses actual words. */
            address[0] = -17;
            address[1] = 23;
            op->read(object, &observed);
            assert(observed.sint[0] == -17 && observed.sint[1] == 23);
            uint64_t padding;
            memcpy(&padding, &object->storage[address == object->storage ? 2 : 0], sizeof(padding));
            assert(padding == UINT64_C(0xa5a5a5a5a5a5a5a5));
        }
        assert(guarded.before == UINT64_C(0x123456789abcdef0));
        assert(guarded.after == UINT64_C(0xfedcba9876543210));
    }
}

#endif
