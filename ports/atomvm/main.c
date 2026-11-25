/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include "avmpack.h"
#include "context.h"
#include "defaultatoms.h"
#include "globalcontext.h"
#include "sys.h"
#include "term.h"
#include "knytos.h"
#include "package.h"
#ifdef KNYTOS_SERVICE_WORKER
#include "service_worker.h"
#endif

bool knytos_atomvm_shutdown_pending(void);

#ifdef AVM_KNYTOS_SCHEDULERS
void knytos_schedulers_prepare(GlobalContext *global);
void knytos_schedulers_join(void);
#else
#define AVM_KNYTOS_SCHEDULERS 1
#endif

int main(void) {
    printf("[atomvm] runtime=AtomVM version=0.6.6 platform=Knytos arch=AArch64 adapter=1 schedulers=%d interpreter=yes\n",
        AVM_KNYTOS_SCHEDULERS);
    printf("[atomvm] package_sha256=%s bytes=%zu\n", package_sha256, sizeof(application_package));
    /* Exercise a real wake published before the sole consumer parks. No
     * producer thread, timer polling or foreign CQ consumer is introduced. */
    knytos_event_signal();
    int startup_status = knytos_event_wait(1000);
    if (startup_status == KNYTOS_SHUTDOWN) knytos_shutdown_complete();
    if (startup_status != KNYTOS_OK) abort();
    puts("[atomvm] wake-before-wait passed");
    if (!avmpack_is_valid(application_package, sizeof(application_package))) return 1;
    GlobalContext *global = globalcontext_new();
    if (!global) return 1;
    struct ConstAVMPack *pack = malloc(sizeof(*pack));
    if (!pack) { globalcontext_destroy(global); return 1; }
    avmpack_data_init(&pack->base, &const_avm_pack_info);
    pack->base.data = application_package;
    pack->base.in_use = true;
    synclist_append(&global->avmpack_data, &pack->base.avmpack_head);
    const void *beam = NULL;
    const char *name = NULL;
    uint32_t size;
    avmpack_find_section_by_flag(application_package, BEAM_START_FLAG, &beam, &size, &name);
    Module *module = beam ? module_new_from_iff_binary(global, beam, size) : NULL;
    if (!module) { globalcontext_destroy(global); return 1; }
    module->module_platform_data = NULL;
    globalcontext_insert_module(global, module);
    Context *context = context_new(global);
    if (!context) { globalcontext_destroy(global); return 1; }
    context->leader = 1;
#ifndef AVM_NO_SMP
    if (knytos_shutdown_via_reactor() != KNYTOS_OK) abort();
    knytos_schedulers_prepare(global);
#endif
#ifdef KNYTOS_SERVICE_WORKER
    knytos_service_worker_start(global);
#endif
    context_execute_loop(context, module, "start", 0);
#ifndef AVM_NO_SMP
    knytos_schedulers_join();
#endif
#ifdef KNYTOS_SERVICE_WORKER
    knytos_service_worker_join();
    puts("[atomvm] service worker joined");
#endif
    if (knytos_atomvm_shutdown_pending()) {
        // The joined workers have settled their native work. Dispose the
        // remaining local VM graph through the tracked C heap, without
        // replaying Erlang process exit callbacks.
        puts("[atomvm] schedulers stopped for shutdown");
        knytos_shutdown_complete();
    }
    term result = context->x[0];
    fputs("[atomvm] return=", stdout);
    term_display(stdout, result, context);
    putchar('\n');
    int status = result == OK_ATOM ? 0 : 1;
    context_destroy(context);
    globalcontext_destroy(global);
    printf("[atomvm] completed status=%d\n", status);
    return status;
}
