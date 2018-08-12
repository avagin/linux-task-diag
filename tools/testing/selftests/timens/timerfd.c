// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>

#include <sys/timerfd.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "log.h"

#ifndef CLONE_NEWTIME
# define CLONE_NEWTIME	0x00001000
#endif

int run_test(int clockid)
{
	struct itimerspec new_value;
	struct timespec now;
	long long elapsed;
	int fd, i;

	if (clock_gettime(clockid, &now))
		return pr_perror("clock_gettime");

	for (i = 0; i < 2; i++) {
		int flags = 0;

		pr_msg("timerfd_settime: %d", "INFO", clockid);
		new_value.it_value.tv_sec = 3600;
		new_value.it_value.tv_nsec = 0;
		new_value.it_interval.tv_sec = 1;
		new_value.it_interval.tv_nsec = 0;

		if (i == 1) {
			new_value.it_value.tv_sec += now.tv_sec;
			new_value.it_value.tv_nsec += now.tv_nsec;
		}

		fd = timerfd_create(clockid, 0);
		if (fd == -1)
			return pr_perror("timerfd_create");

		if (i == 1)
			flags |= TFD_TIMER_ABSTIME;

		if (timerfd_settime(fd, flags, &new_value, NULL))
			return pr_perror("timerfd_settime");

		if (timerfd_gettime(fd, &new_value))
			return pr_perror("timerfd_gettime");

		elapsed = new_value.it_value.tv_sec;
		if (abs(elapsed - 3600) > 60) {
			printf("FAIL\n");
			return 1;
		}

		close(fd);
	}

	printf("PASS\n");

	return 0;
}

int main(int argc, char *argv[])
{
	struct timespec tp;
	int ret;

	if (unshare(CLONE_NEWTIME))
		return pr_perror("unshare");

	if (clock_gettime(CLOCK_MONOTONIC, &tp))
		return pr_perror("clock_gettime");
	tp.tv_sec = 7 * 24 * 3600;
	if (clock_settime(CLOCK_MONOTONIC, &tp))
		return pr_perror("clock_settime");

	if (clock_gettime(CLOCK_BOOTTIME, &tp))
		return pr_perror("clock_gettime");
	tp.tv_sec += 9 * 24 * 3600;
	tp.tv_nsec = 0;
	if (clock_settime(CLOCK_BOOTTIME, &tp))
		return pr_perror("clock_settime");

	ret = 0;
	ret |= run_test(CLOCK_BOOTTIME);
	ret |= run_test(CLOCK_MONOTONIC);
	return ret;
}

