void erts_init_sys_time_sup(void)
{
    ErtsSysInitTimeResult sys_init_time_res
	= ERTS_SYS_INIT_TIME_RESULT_INITER;

    sys_init_time(&sys_init_time_res);

    erts_time_sup__.r.o.monotonic_time_unit
	= sys_init_time_res.os_monotonic_time_unit;

#ifndef SYS_CLOCK_RESOLUTION
    erts_time_sup__.r.o.clktck_resolution
	= sys_init_time_res.sys_clock_resolution;
    erts_time_sup__.r.o.clktck_resolution *= 1000;
#endif

#ifdef ERTS_HAVE_OS_MONOTONIC_TIME_SUPPORT
    time_sup.r.o.os_monotonic_time_disable
	= !sys_init_time_res.have_os_monotonic_time;
    time_sup.r.o.os_corrected_monotonic_time =
	sys_init_time_res.have_corrected_os_monotonic_time;
    time_sup.r.o.os_monotonic_time_func
	= sys_init_time_res.os_monotonic_time_info.func;
    time_sup.r.o.os_monotonic_time_clock_id
	= sys_init_time_res.os_monotonic_time_info.clock_id;
    time_sup.r.o.os_monotonic_time_locked
	= sys_init_time_res.os_monotonic_time_info.locked_use;
    time_sup.r.o.os_monotonic_time_resolution
	= sys_init_time_res.os_monotonic_time_info.resolution;
    time_sup.r.o.os_monotonic_time_used_resolution
	= (!sys_init_time_res.os_monotonic_time_info.used_resolution
           ? sys_init_time_res.os_monotonic_time_info.resolution
           : sys_init_time_res.os_monotonic_time_info.used_resolution);
    time_sup.r.o.os_monotonic_time_extended
	= sys_init_time_res.os_monotonic_time_info.extended;
#endif
    time_sup.r.o.os_system_time_func
	= sys_init_time_res.os_system_time_info.func;
    time_sup.r.o.os_system_time_clock_id
	= sys_init_time_res.os_system_time_info.clock_id;
    time_sup.r.o.os_system_time_locked
	= sys_init_time_res.os_system_time_info.locked_use;
    time_sup.r.o.os_system_time_resolution
	= sys_init_time_res.os_system_time_info.resolution;
}
