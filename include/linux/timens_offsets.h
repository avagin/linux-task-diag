/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIME_OFFSETS_H
#define _LINUX_TIME_OFFSETS_H

/*
 * Time offsets need align as they're placed on vvar page,
 * which should have tail paddings on ia32 vdso.
 * Otherwise as u64 has align(4), vvar offsets will differ.
 * On 64-bit big-endian systems vdso should convert to timespec64
 * to timespec because of a padding occuring between the fields.
 */
struct timens_offsets {
	struct timespec64  monotonic_time_offset __aligned(8);
	struct timespec64  monotonic_boottime_offset __aligned(8);
};

#endif
