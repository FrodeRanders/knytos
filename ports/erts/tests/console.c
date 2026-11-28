/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "erts_time_init.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
#include "global.h"
#include "erl_cpu_topology.h"
#pragma GCC diagnostic pop
#include "erl_printf.h"
#include "ethr_extensions.h"

#ifndef CASE
#define CASE 104
#endif

/* These faults/captures exist only at this fixture's link boundary. Outside
 * the armed startup checks every call delegates to the real implementation. */
static int fail_put, short_write, reject_malloc, capture;
static unsigned put_faults, write_faults;
static char captured[64];
static size_t captured_length;
int __real_fputc(int, FILE *);
size_t __real_fwrite(const void *, size_t, size_t, FILE *);
void *__real_malloc(size_t);
int __wrap_fputc(int, FILE *);
size_t __wrap_fwrite(const void *, size_t, size_t, FILE *);
void *__wrap_malloc(size_t);

int __wrap_fputc(int byte, FILE *stream)
{
    if (fail_put) { fail_put = 0; ++put_faults; errno = EIO; return EOF; }
    if (capture) {
        assert(captured_length < sizeof(captured));
        captured[captured_length++] = (char)byte;
    }
    return __real_fputc(byte, stream);
}

size_t __wrap_fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    if (short_write) {
        short_write = 0; ++write_faults;
        assert(size == 1 && count > 1);
        size_t written = __real_fwrite(buffer, size, count - 1, stream);
        errno = EIO;
        return written;
    }
    return __real_fwrite(buffer, size, count, stream);
}

void *__wrap_malloc(size_t size)
{
    if (reject_malloc) { reject_malloc = 0; return NULL; }
    return __real_malloc(size);
}

static int vout(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = erts_vprintf(format, args);
    va_end(args);
    return result;
}

static int vfout(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = erts_vfprintf(stream, format, args);
    va_end(args);
    return result;
}

static void cpu_prefix(void)
{
    int a,b,c,d,e,f;
    erts_pre_early_init_cpu_topology(&a,&b,&c,&d,&e,&f);
    assert(a > 0 && b > 0 && c == -ENOTSUP && d == -ENOTSUP && e == -ENOTSUP && f == -ENOTSUP);
}

static ethr_event started;
static void *worker(void *argument)
{
    ethr_event_set(&started);
    if (CASE == 107) cpu_prefix();
    for (int i = 0; i < 8; ++i)
        assert(vfout(stderr, "ERTS CONSOLE WORKER n=%d\n", i) == 24);
    return argument;
}

int main(void)
{
    cpu_prefix();
    if (CASE == 106) {
        reject_malloc = 1; /* Reentry must reject before another allocation. */
        cpu_prefix();
        assert(0);
    }
    erts_sys_pre_init();
    assert(erts_fprintf(stdin, "invalid") == -EBADF && errno == EBADF);
    assert(vfout(NULL, "invalid") == -EBADF && errno == EBADF);
    char byte = 'x';
    assert(erts_write_fp(stdout, &byte, (size_t)INT_MAX + 1) == -EOVERFLOW && errno == EOVERFLOW);
    fail_put = 1;
    assert(erts_write_fp(stdout, &byte, 1) == -EIO && !fail_put && put_faults == 1);
    static char long_line[] = "ERTS CONSOLE LONG abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz\n";
    short_write = 1;
    assert(erts_write_fp(stdout, long_line, sizeof(long_line)-1) == -EIO && !short_write && write_faults == 1);
    assert(fputc('\n', stdout) != EOF); /* Finish the already-written prefix. */
    assert(erts_fprintf(stderr, "%s", long_line) == (int)sizeof(long_line)-1);
    static const char crlf[] = "ERTS CONSOLE CRLF\r\n";
    erts_printf_add_cr_to_stdout = 1;
    capture = 1;
    assert(erts_printf("ERTS CONSOLE CRLF\n") == (int)sizeof(crlf)-2);
    capture = 0;
    erts_printf_add_cr_to_stdout = 0;
    assert(captured_length == sizeof(crlf)-1 && !memcmp(captured, crlf, captured_length));
    ethr_late_init_data late = ETHR_LATE_INIT_DATA_DEFAULT_INITER;
    late.main_threads = 1; late.reader_groups = 1;
    assert(ethr_late_init(&late) == 0 && ethr_event_init(&started) == 0);
    if (CASE != 107) { flockfile(stdout); flockfile(stderr); }
    ethr_tid tid;
    assert(ethr_thr_create(&tid, worker, &tid, NULL) == 0);
    assert(ethr_event_wait(&started) == 0);
    if (CASE != 107) {
        assert(vout("ERTS CONSOLE held recursive stream\n") > 0);
        funlockfile(stderr); funlockfile(stdout);
    }
    for (int i = 0; i < 8; ++i)
        assert(vout("ERTS CONSOLE MAIN n=%d\n", i) == 22);
    void *result = NULL;
    assert(ethr_thr_join(tid, &result) == 0 && result == &tid);
    assert(CASE != 107);
    assert(knytos_ethr_threads_shutdown() == 0 && knytos_ethr_ts_events_clear() == 0);
    assert(ethr_event_destroy(&started) == 0);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 1);
    assert(erts_printf("ERTS CONSOLE fixed streams errors CRLF recursive locks and exact join PASS\n") > 0);
    return 0;
}
