/* Explicit pinned contract probe; not a complete sys.h. */
#ifndef KNYTOS_ERTS_TIME_CONTRACT_H
#define KNYTOS_ERTS_TIME_CONTRACT_H
#include "erts_clock_platform.h"
typedef unsigned long Uint64;
typedef struct {
    int have_os_monotonic_time;
    int have_corrected_os_monotonic_time;
    ErtsMonotonicTime os_monotonic_time_unit;
    ErtsMonotonicTime sys_clock_resolution;
    struct {
	Uint64 resolution;
        Uint64 used_resolution;
	char *func;
	char *clock_id;
	int locked_use;
	int extended;
    } os_monotonic_time_info;
    struct {
	Uint64 resolution;
	char *func;
	char *clock_id;
	int locked_use;
    } os_system_time_info;
} ErtsSysInitTimeResult;
#define ERTS_SYS_INIT_TIME_RESULT_INITER \
    {0, 0, (ErtsMonotonicTime) -1, (ErtsMonotonicTime) 1}
extern void sys_init_time(ErtsSysInitTimeResult *);
extern void erts_late_sys_init_time(void);
#endif
