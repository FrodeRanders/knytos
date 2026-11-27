/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_DIAGNOSTICS_H
#define KNYTOS_ETHR_DIAGNOSTICS_H

/* Match OTP's noreturn declarations in standalone consumers too. */
#ifndef ETHR_PROTO_NORETURN__
#define ETHR_PROTO_NORETURN__ _Noreturn void
#endif

ETHR_PROTO_NORETURN__ ethr_assert_failed(const char *file, int line,
                                        const char *function, const char *expression);
ETHR_PROTO_NORETURN__ ethr_fatal_error__(const char *file, int line,
                                       const char *function, int error);

#if defined(DEBUG) && !defined(ETHR_DEBUG)
#define ETHR_DEBUG
#endif
#ifndef ETHR_ASSERT
#ifdef ETHR_DEBUG
#define ETHR_ASSERT(A) ((void)((A) ? (void)1 : ethr_assert_failed(__FILE__, __LINE__, __func__, #A)))
#else
#define ETHR_ASSERT(A) ((void)1)
#endif
#endif
#ifndef ETHR_FATAL_ERROR__
#define ETHR_FATAL_ERROR__(ERR) ethr_fatal_error__(__FILE__, __LINE__, __func__, (ERR))
#endif

#endif
