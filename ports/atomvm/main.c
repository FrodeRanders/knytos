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

int main(void) {
    puts("[atomvm] runtime=AtomVM version=0.6.6 platform=Knytos arch=AArch64 adapter=1 schedulers=1 interpreter=yes");
    printf("[atomvm] package_sha256=%s bytes=%zu\n", package_sha256, sizeof(application_package));
    /* Exercise a real wake published before the sole consumer parks. No
     * producer thread, timer polling or foreign CQ consumer is introduced. */
    knytos_event_signal();
    if (knytos_event_wait(1000) != KNYTOS_OK) abort();
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
    context_execute_loop(context, module, "start", 0);
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
