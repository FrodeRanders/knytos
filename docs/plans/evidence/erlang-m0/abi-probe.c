#include <stdint.h>
#include <stdatomic.h>
_Static_assert(sizeof(void *) == 8, "LP64 pointer");
_Static_assert(sizeof(long) == 8, "LP64 long");
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2, "lock-free pointers");
static volatile uint64_t result;
__attribute__((section(".text._start"), noreturn)) void _start(void) {
    result = 42;
    for (;;) { __asm__ volatile("wfe"); }
}
