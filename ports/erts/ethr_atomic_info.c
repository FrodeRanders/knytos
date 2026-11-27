/* SPDX-License-Identifier: Apache-2.0 */
#include "ethr_atomics.h"

int ethr_init_atomics(void)
{
    /* Every supported width is compile-time native and lock-free. There are
     * no hashed fallback locks, heap reservations or mutable setup state. */
    return 0;
}

static char *const scalar_ops[] = {
    "cmpxchg_mb", "xchg_mb", "set_mb", "init_mb", "add_return_mb", "read_mb",
    "inc_return_mb", "dec_return_mb", "add_mb", "inc_mb", "dec_mb",
    "and_retold_mb", "or_retold_mb", NULL
};

static char *const pair_ops[] = {
    "cmpxchg_mb", "set_mb", "read_mb", "init_mb", NULL
};

static char *const no_ops[] = {NULL};

/* OTP's ABI predates const-correct table signatures. Its BIF consumer only
 * reads these names. They remain borrowed, immutable image-lifetime data. */
char **ethr_native_atomic32_ops(void)
{
    return (char **)scalar_ops;
}

char **ethr_native_atomic64_ops(void)
{
    return (char **)scalar_ops;
}

char **ethr_native_dw_atomic_ops(void)
{
    return (char **)pair_ops;
}

char **ethr_native_su_dw_atomic_ops(void)
{
    /* This backend exposes word-pair primitives, not the optional scalar
     * single-unit double-word native API. */
    return (char **)no_ops;
}
