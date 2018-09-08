/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIME_OFFSETS_H
#define _LINUX_TIME_OFFSETS_H

struct timens_offsets {
	struct timespec64  monotonic_time_offset;
	struct timespec64  monotonic_boottime_offset;
};

#endif
