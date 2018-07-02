/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TIMENS_H__
#define __TIMENS_H__

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../kselftest.h"

#ifndef CLONE_NEWTIME
# define CLONE_NEWTIME	0x00000080
#endif

static inline int _settime(clockid_t clk_id, time_t offset)
{
	int fd, len;
	char buf[4096];

	if (clk_id == CLOCK_MONOTONIC_COARSE || clk_id == CLOCK_MONOTONIC_RAW)
		clk_id = CLOCK_MONOTONIC;

	len = snprintf(buf, sizeof(buf), "%d %ld 0", clk_id, offset);

	fd = open("/proc/self/timens_offsets", O_WRONLY);
	if (fd < 0)
		return pr_perror("/proc/self/timens_offsets");

	if (write(fd, buf, len) != len)
		return pr_perror("/proc/self/timens_offsets");

	close(fd);

	return 0;
}

static inline int _gettime(clockid_t clk_id, struct timespec *res, bool raw_syscall)
{
	int err;

	if (!raw_syscall) {
		if (clock_gettime(clk_id, res)) {
			pr_perror("clock_gettime(%d)", (int)clk_id);
			return -1;
		}
		return 0;
	}

	err = syscall(SYS_clock_gettime, clk_id, res);
	if (err)
		pr_perror("syscall(SYS_clock_gettime(%d))", (int)clk_id);

	return err;
}

static inline void nscheck(void)
{
	if (access("/proc/self/ns/time", F_OK) < 0)
		ksft_exit_skip("Time namespaces are not supported\n");
}

#endif
