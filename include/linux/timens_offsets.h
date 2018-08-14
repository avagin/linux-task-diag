/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIME_OFFSETS_H
#define _LINUX_TIME_OFFSETS_H

/*
 * Time offsets need align as they're placed on VVAR page,
 * which is used by x86_64 and ia32 VDSO code.
 * On ia32 offset::tv_sec (u64) has align(4), so re-align offsets
 * to the same positions as 64-bit offsets.
 * On 64-bit big-endian systems VDSO should convert to timespec64
 * to timespec because of a padding occurring between the fields.
 */
struct timens_offsets {
	struct timespec64 monotonic __aligned(8);
	struct timespec64 boottime __aligned(8);
};

#endif
