/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdlib.h>

#include "knytos.h"
#include "ethr_tsd.h"

static int tsd_status(int status)
{
    switch (status) {
        case KNYTOS_OK: return 0;
        case KNYTOS_INVALID: return EINVAL;
        case KNYTOS_UNAVAILABLE: return EAGAIN;
        case KNYTOS_OVERFLOW: return EOVERFLOW;
        default: {
            static const char message[] = "[erts] TSD operation failed\n";
            knytos_console_write(message, sizeof(message) - 1);
            abort();
        }
    }
}

int ethr_tsd_key_create(ethr_tsd_key *key, char *name)
{
    (void)name;
    return tsd_status(knytos_tsd_key_create(key));
}

int ethr_tsd_key_delete(ethr_tsd_key key)
{
    return tsd_status(knytos_tsd_key_delete(key));
}

int ethr_tsd_set(ethr_tsd_key key, void *value)
{
    return tsd_status(knytos_tsd_set(key, value));
}

void *ethr_tsd_get(ethr_tsd_key key)
{
    return knytos_tsd_get(key);
}
