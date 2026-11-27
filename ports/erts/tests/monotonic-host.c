/* SPDX-License-Identifier: Apache-2.0 */
/* Deterministic fault harness; never linked into a guest image. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "knytos.h"
#include "erts_monotonic.h"

static uint64_t value;
static int32_t status;
static unsigned calls;
static int terminal;
static int diagnosed;

int32_t knytos_clock_monotonic_ns(uint64_t *output)
{
    assert(++calls == 1);
    if (status == KNYTOS_OK) *output = value;
    return status;
}

int32_t knytos_console_write(const void *buffer, size_t length)
{
    static const char expected[] = "[erts] monotonic clock unavailable or out of range\n";
    assert(terminal && length == sizeof(expected) - 1);
    assert(memcmp(buffer, expected, length) == 0);
    diagnosed = 1;
    return KNYTOS_UNAVAILABLE; /* Failed logging must still abort. */
}

_Noreturn void abort(void)
{
    _exit(terminal && diagnosed && calls == 1 ? 42 : 99);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    int mode = atoi(argv[1]);
    terminal = mode >= 3;
    switch (mode) {
    case 0: value = 0; break;
    case 1: value = 123456789; break;
    case 2: value = LONG_MAX; break;
    case 3: value = (uint64_t)LONG_MAX + 1; break;
    case 4: value = UINT64_MAX; break;
    default: status = mode - 4; break; /* Every current nonzero ABI status. */
    }
    long result = atoi(argv[2]) ? erts_sys_hrtime() : erts_os_monotonic_time();
    assert(!terminal && calls == 1 && result == (long)value);
    return 0;
}
