/* SPDX-License-Identifier: Apache-2.0 */
/* Run the real formatter with terminal/console boundaries replaced. This
 * does not simulate kernel abort or qualify the native stdio lock bypass. */
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knytos.h"
#include "ethr_diagnostics.h"

static jmp_buf terminal;
static char output[241];
static size_t written;
static unsigned writes, aborts;
static int console_status;
static int fatal_evaluated;

static void check(int condition)
{
    if (!condition) {
        fputs("ERTS diagnostic host check failed\n", stderr);
        _Exit(1);
    }
}

int32_t knytos_console_write(const void *bytes, size_t length)
{
    check(length > 0 && length <= 240);
    ++writes;
    written = length;
    memcpy(output, bytes, length);
    output[length] = '\0';
    return console_status;
}

_Noreturn void abort(void)
{
    ++aborts;
    longjmp(terminal, 1);
}

static void reset(void)
{
    writes = aborts = 0;
    memset(output, 0, sizeof(output));
    written = 0;
}

static void terminal_report(void)
{
    check(writes == 1 && aborts == 1);
    check(written == strlen(output) && output[written - 1] == '\n');
    check(strchr(output, '\n') == output + written - 1);
}

int main(void)
{
    int evaluated = 0;
    ETHR_ASSERT(++evaluated);
#ifdef ETHR_DEBUG
    check(evaluated == 1);
#else
    check(evaluated == 0);
#endif

    reset();
    if (!setjmp(terminal)) ethr_fatal_error__("source.c", 17, "caller", INT_MIN);
    terminal_report();
    check(strcmp(output, "[erts] fatal error file=source.c line=17 function=caller error=-2147483648\n") == 0);

    reset();
    if (!setjmp(terminal)) ethr_fatal_error__(NULL, INT_MIN, NULL, INT_MAX);
    terminal_report();
    check(strcmp(output, "[erts] fatal error file=(null) line=-2147483648 function=(null) error=2147483647\n") == 0);

    reset();
    if (!setjmp(terminal)) ethr_assert_failed("src\n\t.c", -1, "func\r", "value\n\177");
    terminal_report();
    check(strcmp(output, "[erts] assertion failed file=src??.c line=-1 function=func? expression=value??\n") == 0);

    reset();
    if (!setjmp(terminal)) ethr_assert_failed(NULL, 0, NULL, NULL);
    terminal_report();
    check(strstr(output, "expression=(null)\n") != NULL);

    char long_field[1024];
    memset(long_field, 'x', sizeof(long_field));
    /* Deliberately no NUL: each field is bounded before any unbounded scan. */
    reset();
    if (!setjmp(terminal)) ethr_assert_failed(long_field, INT_MIN, long_field, long_field);
    terminal_report();
    check(strstr(output, "... line=-2147483648") != NULL);
    check(strstr(output, "... expression=") != NULL);
    check(strcmp(output + written - 4, "...\n") == 0);

    reset();
    console_status = KNYTOS_UNAVAILABLE;
    if (!setjmp(terminal)) ETHR_FATAL_ERROR__(++fatal_evaluated);
    terminal_report();
    check(fatal_evaluated == 1 && strstr(output, "function=main error=1\n") != NULL);
    puts("ERTS DIAGNOSTICS HOST bounded fields signed limits macros and failed console PASS");
    return 0;
}
