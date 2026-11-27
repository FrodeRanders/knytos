/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#include "knytos.h"
#include "ethr_diagnostics.h"

/* No stdio, allocation, strerror storage or shared formatter state on a
 * terminal path. Keep one bounded write, including its terminating newline.
 * The runtime console still serializes output; this is not an IRQ/signal
 * handler or a reentrant console implementation. */
struct report {
    char bytes[240];
    size_t length;
};

static void put(struct report *report, char byte)
{
    if (report->length < sizeof(report->bytes) - 1)
        report->bytes[report->length++] = byte;
}

static void text(struct report *report, const char *string, size_t limit)
{
    if (!string) string = "(null)";
    size_t index = 0;
    for (; index < limit && string[index]; ++index) {
        unsigned char byte = (unsigned char)string[index];
        put(report, byte < 32 || byte == 127 ? '?' : (char)byte);
    }
    /* Reaching the field limit never requires scanning the remaining input. */
    if (index == limit) {
        put(report, '.');
        put(report, '.');
        put(report, '.');
    }
}

static void number(struct report *report, int value)
{
    unsigned magnitude = (unsigned)value;
    if (value < 0) {
        put(report, '-');
        magnitude = 0u - magnitude; /* Defined even for INT_MIN. */
    }
    char reversed[sizeof(unsigned) * CHAR_BIT];
    size_t length = 0;
    do {
        reversed[length++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    while (length) put(report, reversed[--length]);
}

static void location(struct report *report, const char *file, int line, const char *function)
{
    text(report, " file=", 16);
    text(report, file, 48);
    text(report, " line=", 16);
    number(report, line);
    text(report, " function=", 16);
    text(report, function, 40);
}

static _Noreturn void terminate(struct report *report)
{
    report->bytes[report->length++] = '\n';
    /* A failed diagnostic must still take the terminal path. Abort owns
     * domain termination; it does not run C finalizers or worker teardown. */
    (void)knytos_console_write(report->bytes, report->length);
    abort();
}

_Noreturn void ethr_assert_failed(const char *file, int line,
                                 const char *function, const char *expression)
{
    struct report report = {0};
    text(&report, "[erts] assertion failed", 32);
    location(&report, file, line, function);
    text(&report, " expression=", 16);
    text(&report, expression, 64);
    terminate(&report);
}

_Noreturn void ethr_fatal_error__(const char *file, int line, const char *function, int error)
{
    struct report report = {0};
    text(&report, "[erts] fatal error", 32);
    location(&report, file, line, function);
    text(&report, " error=", 16);
    number(&report, error);
    terminate(&report);
}
