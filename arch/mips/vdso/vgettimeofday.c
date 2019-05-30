// SPDX-License-Identifier: GPL-2.0
/*
 * MIPS64 and compat userspace implementations of gettimeofday()
 * and similar.
 *
 * Copyright (C) 2018 ARM Limited
 *
 */
#include <linux/time.h>
#include <linux/types.h>

#if _MIPS_SIM != _MIPS_SIM_ABI64
notrace int __vdso_clock_gettime(clockid_t clock,
				 struct old_timespec32 *ts)
{
	return __cvdso_clock_gettime32(clock, ts);
}

notrace int __vdso_gettimeofday(struct __kernel_old_timeval *tv,
				struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

notrace int __vdso_clock_getres(clockid_t clock_id,
				struct old_timespec32 *res)
{
	return __cvdso_clock_getres_time32(clock_id, res);
}

notrace int __vdso_clock_gettime_time64(clockid_t clock,
				 struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

#else

notrace int __vdso_clock_gettime(clockid_t clock,
				 struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

notrace int __vdso_gettimeofday(struct __kernel_old_timeval *tv,
				struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

notrace int __vdso_clock_getres(clockid_t clock_id,
				struct __kernel_timespec *res)
{
	return __cvdso_clock_getres(clock_id, res);
}

#endif
