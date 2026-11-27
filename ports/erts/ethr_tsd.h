/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KNYTOS_ETHR_TSD_H
#define KNYTOS_ETHR_TSD_H

#include "ethr_native_types.h"

int ethr_tsd_key_create(ethr_tsd_key *key, char *name);
int ethr_tsd_key_delete(ethr_tsd_key key);
int ethr_tsd_set(ethr_tsd_key key, void *value);
void *ethr_tsd_get(ethr_tsd_key key);

#endif
