/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
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
#include "erl_time.h"
#pragma GCC diagnostic pop
#include "ethr_extensions.h"
#include "calendar-values.h"

#ifndef CASE
#define CASE 96
#endif

static void *worker(void *argument)
{
    if (CASE == 100) erts_sys_pre_init();
    assert(erts_time_sup__.r.o.monotonic_time_unit == 1000000000);
    assert(ethr_no_used_tse() == 2);
    calendar_current();
    return argument;
}

int main(void)
{
    assert(erts_time_sup__.r.o.monotonic_time_unit == 0);
    ErtsSystemTime cold = -123;
    assert(knytos_erts_clock_read(&cold) == KNYTOS_UNAVAILABLE && cold == -123);
    if (CASE == 98) {
        uint64_t keys[15];
        for (unsigned i = 0; i < 15; ++i)
            assert(knytos_tsd_key_create(&keys[i]) == KNYTOS_OK);
    }
    erts_sys_pre_init();
    assert(CASE != 98 && CASE != 99);
    assert(erts_time_sup__.r.o.monotonic_time_unit == 1000000000);
    assert(knytos_erts_clock_read(&cold) == KNYTOS_OK && cold > 1577836800000000000L);
    if (CASE == 97) erts_sys_pre_init();
    assert(CASE != 97);
    assert(ethr_no_used_tse() == 0);
    ethr_tid tid = {99, 99};
    assert(ethr_thr_create(&tid, worker, NULL, NULL) == EACCES);
    assert(tid.slot == 99 && tid.generation == 99);
    struct knytos_allocation_stats allocations;
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    calendar_values();
    calendar_current();
    ethr_late_init_data late = ETHR_LATE_INIT_DATA_DEFAULT_INITER;
    late.main_threads = 1;
    late.reader_groups = 1;
    assert(ethr_late_init(&late) == 0 && ethr_no_used_tse() == 1);
    int argument = 123;
    assert(ethr_thr_create(&tid, worker, &argument, NULL) == 0);
    calendar_current();
    void *result = NULL;
    assert(ethr_thr_join(tid, &result) == 0 && result == &argument);
    assert(CASE != 100);
    assert(knytos_ethr_threads_shutdown() == 0);
    assert(ethr_no_used_tse() == 1);
    assert(knytos_ethr_ts_events_clear() == 0 && ethr_no_used_tse() == 0);
    assert(knytos_allocation_stats(&allocations) == KNYTOS_OK && allocations.blocks == 0);
    puts("ERTS PRE INIT common time metadata early gate late worker handoff and zero owners PASS");
    return 0;
}
