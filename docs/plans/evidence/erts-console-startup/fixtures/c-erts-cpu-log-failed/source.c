/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
#include "sys.h"
#pragma GCC diagnostic pop
#include "erts_time_init.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
#include "global.h"
#include "erl_cpu_topology.h"
#include "erl_time.h"
#pragma GCC diagnostic pop
#include "erl_misc_utils.h"
#include "erl_printf.h"
#include "ethr_extensions.h"

#ifndef CASE
#define CASE 101
#endif

/* Test-link fault injection only. Successful allocations always delegate to
 * the actual C allocator. No production library redirects malloc. */
static int reject_allocation;
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_malloc(size_t size)
{
    if (reject_allocation) { reject_allocation = 0; return NULL; }
    return __real_malloc(size);
}

static size_t blocks(void)
{
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK);
    return stats.blocks;
}

#ifdef KNYTOS_ERTS_CONSOLE_CHECK
static int outputs[6], checking_cpu_failure, diagnostic_rejected;
int32_t __real_knytos_console_write(const void *, size_t);
int32_t __wrap_knytos_console_write(const void *, size_t);
int32_t __wrap_knytos_console_write(const void *buffer, size_t size)
{
    static const char diagnostic[] = "[erts] CPU-info allocation failed\n";
    if (CASE == 108 && checking_cpu_failure && size == sizeof(diagnostic)-1 &&
        !memcmp(buffer, diagnostic, size)) {
        diagnostic_rejected = 1;
        return KNYTOS_UNAVAILABLE;
    }
    return __real_knytos_console_write(buffer, size);
}
_Noreturn void __real_abort(void);
_Noreturn void __wrap_abort(void);
_Noreturn void __wrap_abort(void)
{
    if (checking_cpu_failure) {
        checking_cpu_failure = 0;
        for (unsigned i = 0; i < 6; ++i) assert(outputs[i] == 0x5a5a);
        assert(blocks() == 0);
        assert(CASE != 108 || diagnostic_rejected);
        puts("ERTS CPU rejected preparation preserves outputs and owns zero blocks PASS");
    }
    __real_abort();
}
#endif

static void unknown_cpu(void)
{
    erts_cpu_info_t *info = erts_cpu_info_create();
    assert(info);
    for (int skip = 0; skip < 2; ++skip) {
        assert(erts_cpu_info_update(info, skip) == 0);
        assert(erts_get_cpu_configured(info) == -ENOTSUP);
        assert(erts_get_cpu_online(info) == -ENOTSUP);
        assert(erts_get_cpu_available(info) == -ENOTSUP);
        assert(erts_get_cpu_quota(info) == -ENOTSUP);
        assert(erts_get_cpu_topology_size(info) == 0);
        erts_cpu_topology_t output, original;
        memset(&output, 0xa5, sizeof(output)); original = output;
        assert(erts_get_cpu_topology(info, &output) == 0);
        assert(!memcmp(&output, &original, sizeof(output)));
        assert(erts_get_available_cpu(info, -1) == -EINVAL);
        assert(erts_get_available_cpu(info, 1) == -EINVAL);
        assert(!erts_is_cpu_available(info, 0));
        assert(erts_bind_to_cpu(info, -1) == -ENOTSUP);
        assert(erts_bind_to_cpu(info, 0) == -ENOTSUP);
        assert(erts_unbind_from_cpu(info) == -ENOTSUP);
        assert(erts_unbind_from_cpu_str("1") == -ENOTSUP);
    }
    erts_cpu_info_destroy(info);
}

static void formatting(void)
{
    char output[128];
    static const char expected[] = "native -9223372036854775808 18446744073709551615 0000cafe";
    int length = erts_snprintf(output, sizeof(output), "%s %lld %llu %08x",
                               "native", LLONG_MIN, ULLONG_MAX, 0xcafe);
    assert(length >= 0 && (size_t)length == sizeof(expected)-1 && !strcmp(output, expected));
    char small[4];
    assert(erts_snprintf(small, sizeof(small), "%s", "abcdef") == 6 && !strcmp(small, "abc"));
    assert(erts_snprintf(output, 0, "%s", "unchanged") == -EINVAL);
}

static void *worker(void *argument)
{
    assert(erts_time_sup__.r.o.monotonic_time_unit == 1000000000);
    unknown_cpu();
    formatting();
    assert(erts_os_system_time() > 1577836800000000000L);
    return argument;
}

int main(void)
{
    assert(blocks() == 0);
    reject_allocation = 1;
    assert(erts_cpu_info_create() == NULL && !reject_allocation && blocks() == 0);
    unknown_cpu();
    assert(blocks() == 0);
    assert(erts_get_cpu_configured(NULL) == -EINVAL && erts_get_cpu_online(NULL) == -EINVAL);
    assert(erts_get_cpu_available(NULL) == -EINVAL && erts_get_cpu_quota(NULL) == -EINVAL);
    assert(erts_bind_to_cpu(NULL, 0) == -EINVAL && erts_unbind_from_cpu(NULL) == -EINVAL);
    erts_cpu_info_destroy(NULL);
    if (CASE == 102 || CASE == 108) {
        reject_allocation = 1;
#ifdef KNYTOS_ERTS_CONSOLE_CHECK
        for (unsigned i = 0; i < 6; ++i) outputs[i] = 0x5a5a;
        checking_cpu_failure = 1;
        erts_pre_early_init_cpu_topology(&outputs[0], &outputs[1], &outputs[2], &outputs[3], &outputs[4], &outputs[5]);
        assert(0 && "native CPU allocation failure must terminate");
#endif
    }
    int dcg, readers, configured, online, available, quota;
    erts_pre_early_init_cpu_topology(&dcg, &readers, &configured, &online, &available, &quota);
    assert(dcg > 0 && readers > 0 && !reject_allocation);
    int unknown = CASE == 102 ? -EINVAL : -ENOTSUP;
    assert(configured == unknown && online == unknown && available == unknown && quota == unknown);
    size_t retained = CASE == 102 ? 0 : 1;
    assert(blocks() == retained);
    if (CASE == 102) {
        /* The later common topology initializer requires a live CPU-info
         * object. Stop this fixture at rejected preparation; do not publish
         * native threads or imply that full startup can continue from NULL. */
        puts("ERTS INTERNAL CPU common allocation rejection zero owners before native pre-init PASS");
        return 0;
    }
    erts_sys_pre_init();
    assert(erts_time_sup__.r.o.monotonic_time_unit == 1000000000);
    ethr_late_init_data late = ETHR_LATE_INIT_DATA_DEFAULT_INITER;
    late.main_threads = 1; late.reader_groups = 1;
    assert(ethr_late_init(&late) == 0);
    ethr_tid tid;
    assert(ethr_thr_create(&tid, worker, &configured, NULL) == 0);
    formatting();
    void *result = NULL;
    assert(ethr_thr_join(tid, &result) == 0 && result == &configured);
    assert(knytos_ethr_threads_shutdown() == 0);
    assert(knytos_ethr_ts_events_clear() == 0);
    /* The common CPU-info object is a runtime-lifetime owner with no public
     * shutdown API. Normal C runtime exit frees it after all workers drain. */
    assert(blocks() == retained);
    puts("ERTS INTERNAL CPU unknown counts allocation rejection common prefix formatter and exact join PASS");
    return 0;
}
