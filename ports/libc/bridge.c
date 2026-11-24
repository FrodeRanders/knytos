#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "knytos.h"

extern void *knytos_malloc(size_t);
extern void *knytos_calloc(size_t, size_t);
extern void *knytos_realloc(void *, size_t);
extern void *knytos_aligned_alloc(size_t, size_t);
extern void knytos_free(void *);
extern _Noreturn void knytos_c_terminate(int, int);
extern _Noreturn void knytos_c_abort(void);

void *malloc(size_t n) { void *p = knytos_malloc(n); if (!p) errno = ENOMEM; return p; }
void *calloc(size_t n, size_t size) { void *p = knytos_calloc(n, size); if (!p) errno = ENOMEM; return p; }
void *realloc(void *p, size_t n) { void *q = knytos_realloc(p, n); if (!q && n) errno = ENOMEM; return q; }
void free(void *p) { knytos_free(p); }
void *aligned_alloc(size_t align, size_t n) {
    if (!align || (align & (align - 1)) || n % align) { errno = EINVAL; return NULL; }
    void *p = knytos_aligned_alloc(align, n); if (!p) errno = ENOMEM; return p;
}

static int put_character(char c, FILE *stream) {
    (void)stream;
    return knytos_console_write(&c, 1) == KNYTOS_OK ? 0 : EOF;
}
static int flush_stream(FILE *stream) { (void)stream; knytos_console_flush(); return 0; }
static int unavailable_input(FILE *stream) { (void)stream; errno = ENOSYS; return EOF; }
static FILE console = FDEV_SETUP_STREAM(put_character, NULL, flush_stream, _FDEV_SETUP_WRITE);
static FILE input = FDEV_SETUP_STREAM(NULL, unavailable_input, NULL, _FDEV_SETUP_READ);
FILE *const stdout = &console;
FILE *const stderr = &console;
/* Input is unavailable; this profile supplies no fake successful read. */
FILE *const stdin = &input;

_Noreturn void exit(int status) {
    fflush(stdout);
    fflush(stderr);
    knytos_c_terminate(status, 1);
}
_Noreturn void _Exit(int status) { knytos_c_terminate(status, 0); }
_Noreturn void _exit(int status) { _Exit(status); }
_Noreturn void abort(void) { knytos_c_abort(); }
