#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "knytos.h"

#ifndef CASE
#define CASE 0
#endif
static volatile int initialized;
__attribute__((constructor(101))) static void initialize(void) { initialized = 41; }
__attribute__((constructor(102))) static void initialize_second(void) { assert(initialized == 41); ++initialized; }
__attribute__((destructor)) static void finish(void) { puts("CABI FINI"); }
static void on_exit_test(void) { puts("CABI ATEXIT"); }

int main(int argc, char **argv) {
    assert(argc == 1 && argv && argv[0] && !strcmp(argv[0], "knytos-c") && argv[1] == NULL);
    assert(knytos_abi_version() == 1 && initialized == 42);
    if (CASE == 0) {
        puts("CABI hello: C on Knytos");
        struct knytos_abi_pair p = knytos_abi_transform((struct knytos_abi_pair){123, -17, 1.25}, 9);
        assert(p.unsigned_value == 132 && p.signed_value == -26 && p.floating_value == 2.5);
    } else if (CASE == 1) {
        unsigned char *p = aligned_alloc(4096, 4096);
        assert(p && (uintptr_t)p % 4096 == 0);
        memset(p, 0x5a, 4096);
        volatile size_t huge = SIZE_MAX;
        assert(realloc(p, huge) == NULL);
        for (size_t i = 0; i < 4096; ++i) assert(p[i] == 0x5a);
        p = realloc(p, 8192); assert(p && (uintptr_t)p % 4096 == 0);
        for (size_t i = 0; i < 4096; ++i) assert(p[i] == 0x5a);
        free(p);
        p = calloc(257, 3); assert(p);
        for (size_t i = 0; i < 771; ++i) assert(p[i] == 0);
        free(p); free(NULL);
        assert(calloc(huge, 2) == NULL);
        volatile size_t invalid_alignment = 3;
        assert(aligned_alloc(invalid_alignment, 9) == NULL && errno == EINVAL);
        void *blocks[96]; size_t n = 0;
        while (n < 96 && (blocks[n] = malloc(65536))) ++n;
        assert(n > 0 && n < 96 && errno == ENOMEM);
        while (n) free(blocks[--n]);
        struct knytos_allocation_stats stats;
        assert(knytos_allocation_stats(&stats) == 0 && stats.blocks == 0 && stats.requested_bytes == 0);
        void *zero = malloc(0); assert(zero); free(zero);
    } else if (CASE == 2) {
        char text[128];
        assert(snprintf(text, sizeof(text), "%s %d %llx %.2f", "value", -42, 0x123456789abcdef0ULL, 2.5) > 0);
        assert(!strcmp(text, "value -42 123456789abcdef0 2.50"));
        char short_text[4];
        assert(snprintf(short_text, sizeof(short_text), "%s", "abcdef") == 6 && !strcmp(short_text, "abc"));
        assert(strtol("-1234", NULL, 10) == -1234);
        volatile double value = 9.0;
        assert(sqrt(value) == 3.0);
        printf("CABI formatting: %s\n", text);
    } else if (CASE == 3) {
        uint64_t first, second, untouched = 123;
        assert(knytos_clock_monotonic_ns(&first) == 0);
        assert(knytos_clock_monotonic_ns(&second) == 0 && second >= first);
        assert(knytos_clock_monotonic_ns(NULL) == KNYTOS_INVALID);
        assert(knytos_clock_realtime_ns(&untouched) == KNYTOS_UNAVAILABLE && untouched == 123);
        assert(knytos_console_write(NULL, 1) == KNYTOS_INVALID);
    } else if (CASE == 4) {
        assert(atexit(on_exit_test) == 0);
        /* Runtime exit custody must free a block deliberately left live. */
        assert(malloc(64) != NULL);
    } else if (CASE == 5) {
        assert(atexit(on_exit_test) == 0);
        assert(malloc(32) != NULL);
        puts("CABI explicit exit");
        exit(0);
    } else if (CASE == 6) {
        assert(malloc(64) != NULL);
        puts("CABI expected nonzero");
        return 17;
    } else if (CASE == 7) {
        puts("CABI expected abort");
        abort();
    }
    printf("CABI PASS case=%d\n", CASE);
    return 0;
}
