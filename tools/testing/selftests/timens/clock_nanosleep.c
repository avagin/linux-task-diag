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
#include "timens.h"

static long long get_elapsed_time(int clockid, struct timespec *start)
{
	struct timespec curr;
	long long secs, nsecs;

	if (clock_gettime(clockid, &curr) == -1)
		return pr_perror("clock_gettime");

	secs = curr.tv_sec - start->tv_sec;
	nsecs = curr.tv_nsec - start->tv_nsec;
	if (nsecs < 0) {
		secs--;
		nsecs += 1000000000;
	}
	if (nsecs > 1000000000) {
		secs++;
		nsecs -= 1000000000;
	}
	return secs * 1000 + nsecs / 1000000;
}

int run_test(int clockid)
{
	long long elapsed;
	int i;

	for (i = 0; i < 2; i++) {
		struct timespec now = {};
		struct timespec start;

		if (clock_gettime(clockid, &start) == -1)
			return pr_perror("clock_gettime");


		if (i == 1) {
			now.tv_sec = start.tv_sec;
			now.tv_nsec = start.tv_nsec;
		}

		now.tv_sec += 2;
		clock_nanosleep(clockid, i ? TIMER_ABSTIME : 0, &now, NULL);

		elapsed = get_elapsed_time(clockid, &start);
		if (elapsed < 1900 || elapsed > 2100) {
			pr_fail("clockid: %d abs: %d elapsed: %lld\n",
				clockid, i, elapsed);
			return 1;
		}
		ksft_test_result_pass("clockid: %d abs:%d\n", clockid, i);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int ret, nsfd;

	nscheck();

	ksft_set_plan(4);

	if (unshare(CLONE_NEWTIME))
		return pr_perror("unshare");

	if (_settime(CLOCK_MONOTONIC, 7 * 24 * 3600))
		return 1;
	if (_settime(CLOCK_BOOTTIME, 9 * 24 * 3600))
		return 1;

	nsfd = open("/proc/self/ns/time_for_children", O_RDONLY);
	if (nsfd < 0)
		return pr_perror("Unable to open timens_for_children");

	if (setns(nsfd, CLONE_NEWTIME))
		return pr_perror("Unable to set timens");

	ret = 0;
	ret |= run_test(CLOCK_MONOTONIC);
	ret |= run_test(CLOCK_BOOTTIME_ALARM);

	if (ret)
		ksft_exit_fail();
	ksft_exit_pass();
	return ret;
}

