#include <assert.h>
#include "ethr_atomics.h"
#include "atomics-values.h"
#include "dw-atomics-values.h"
int main(void) {
    assert(ethr_init_atomics() == 0);
    check_atomic_values();
    check_dw_values();
    assert(ethr_init_atomics() == 0);
    assert(ethr_native_atomic32_ops()[0] != 0);
    assert(ethr_native_atomic64_ops()[0] != 0);
    assert(ethr_native_dw_atomic_ops()[0] != 0);
    assert(ethr_native_su_dw_atomic_ops()[0] == 0);
    return 0;
}
