/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>

#include "../ethr_thread.c"
#include "../ethr_event.c"

struct input {
    ethr_tid tid;
    ethr_event gate;
    ethr_event ready;
    ethr_event finish;
    const char *initial;
    uint64_t key;
    unsigned cleaned;
};

static void name_cleanup(void *argument)
{
    struct input *input = argument;
    char buffer[16];
    assert(ethr_getname(ethr_self(), buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, "renamed") == 0);
    input->cleaned++;
}

static void *named_entry(void *argument)
{
    struct input *input = argument;
    assert(ethr_event_wait(&input->gate) == 0);
    char buffer[16];
    assert(ethr_getname(ethr_self(), buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, input->initial) == 0);
    ethr_setname("sixteen-letters!");
    assert(ethr_getname(ethr_self(), buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, input->initial) == 0);
    ethr_event_set(&input->ready);
    for (unsigned i = 0; i < 1000; ++i)
        ethr_setname(i & 1 ? "aaaaaaaaaaaaaaa" : "bbbbbbbbbbbbbbb");
    ethr_setname("renamed");
    assert(ethr_event_wait(&input->finish) == 0);
    assert(knytos_tsd_set(input->key, input) == KNYTOS_OK);
    return argument;
}

int main(void)
{
    char buffer[16];
    ethr_setname("startup");
    assert(ethr_getname(ethr_self(), buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, "startup") == 0);
    struct input input = { .initial = "123456789abcdef" };
    assert(ethr_event_init(&input.gate) == 0);
    assert(ethr_event_init(&input.ready) == 0);
    assert(ethr_event_init(&input.finish) == 0);
    assert(knytos_tsd_key_create_with_destructor(&input.key, name_cleanup) == KNYTOS_OK);
    char source[] = "123456789abcdef";
    ethr_thr_opts options = ETHR_THR_OPTS_DEFAULT_INITER;
    options.name = source;
    for (unsigned generation = 0; generation < 2; ++generation) {
        ethr_tid previous = input.tid;
        assert(ethr_thr_create(&input.tid, named_entry, &input, &options) == 0);
        assert(ethr_getname(input.tid, buffer, sizeof(buffer)) == 0);
        assert(strcmp(buffer, input.initial) == 0);
        strcpy(source, "caller changed");
        char short_buffer[] = "keep";
        assert(ethr_getname(input.tid, short_buffer, sizeof(short_buffer)) == ERANGE);
        assert(strcmp(short_buffer, "keep") == 0);
        if (generation) {
            assert(ethr_getname(previous, buffer, sizeof(buffer)) == EINVAL);
        }
        ethr_event_set(&input.gate);
        assert(ethr_event_wait(&input.ready) == 0);
        /* Deliberately bounded concurrent snapshot exercise. EAGAIN leaves
         * the output untouched, and every successful snapshot is complete. */
        for (unsigned i = 0; i < 2000; ++i) {
            strcpy(buffer, "unchanged");
            int status = ethr_getname(input.tid, buffer, sizeof(buffer));
            if (status == EAGAIN) {
                assert(strcmp(buffer, "unchanged") == 0);
            } else {
                assert(status == 0);
                assert(strcmp(buffer, input.initial) == 0
                    || strcmp(buffer, "aaaaaaaaaaaaaaa") == 0
                    || strcmp(buffer, "bbbbbbbbbbbbbbb") == 0
                    || strcmp(buffer, "renamed") == 0);
            }
        }
        ethr_event_set(&input.finish);
        assert(ethr_thr_join(input.tid, NULL) == 0);
        assert(input.cleaned == generation + 1);
        assert(ethr_getname(input.tid, buffer, sizeof(buffer)) == EINVAL);
        ethr_event_reset(&input.gate);
        ethr_event_reset(&input.ready);
        ethr_event_reset(&input.finish);
        strcpy(source, input.initial);
    }
    assert(ethr_event_destroy(&input.gate) == 0);
    assert(ethr_event_destroy(&input.ready) == 0);
    assert(ethr_event_destroy(&input.finish) == 0);
    assert(knytos_tsd_key_delete(input.key) == KNYTOS_OK);
    struct knytos_allocation_stats stats;
    assert(knytos_allocation_stats(&stats) == KNYTOS_OK && stats.blocks == 0);
    puts("ERTS NAMES copied names coherent snapshots and buffer bounds");
    puts("ERTS NAMES destructor lookup and stale generations");
    puts("ERTS NAMES PASS");
    return 0;
}
