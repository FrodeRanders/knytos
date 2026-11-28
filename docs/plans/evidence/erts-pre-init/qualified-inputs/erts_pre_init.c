/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_threads.h"
#include "erl_time.h"

#if defined(ERTS_ENABLE_LOCK_COUNT) || defined(ERTS_ENABLE_LOCK_CHECK) || defined(ERTS_DYN_LOCK_CHECK)
#error "Native pre-init does not yet support ERTS lock instrumentation"
#endif
#ifdef ERTS_THR_HAVE_SIG_FUNCS
#error "Native pre-init has no Unix signal-mask contract"
#endif

void erts_sys_pre_init(void)
{
    /* The initial native profile is unbound and has no Unix signal state to
     * inherit or repair. Native ethread owns identity/event key preparation;
     * late initialization must still precede every ERTS worker creation. */
    ethr_init_data data = ETHR_INIT_DATA_DEFAULT_INITER;
    int status = ethr_init(&data);
    if (status) ETHR_FATAL_ERROR__(status);

    /* Run the actual common initializer, which consumes sys_init_time's
     * prepared record. A clock error terminates this partial startup domain;
     * committed thread registrations are never replayed or released early. */
    erts_init_sys_time_sup();
}
