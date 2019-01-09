// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "log.h"
#include "timens.h"

//#define TEST_SYSCALL

static void test(clock_t clockid, char *clockstr, bool in_ns)
{
	struct timespec tp, start;
	long i = 0;
	const int timeout = 3;

#ifndef TEST_SYSCALL
	clock_gettime(clockid, &start);
#else
	syscall(__NR_clock_gettime, clockid, &start);
#endif
	tp = start;
	for (tp = start; start.tv_sec + timeout > tp.tv_sec ||
			 (start.tv_sec + timeout == tp.tv_sec &&
			  start.tv_nsec > tp.tv_nsec); i++) {
#ifndef TEST_SYSCALL
		clock_gettime(clockid, &tp);
#else
		syscall(__NR_clock_gettime, clockid, &tp);
#endif
	}

	ksft_test_result_pass("%s:\tclock: %10s\tcycles:\t%10ld\n",
			      in_ns ? "ns" : "host", clockstr, i);
}

int main(int argc, char *argv[])
{
	time_t offset = 10;
	int nsfd;

	test(CLOCK_MONOTONIC, "monotonic", false);
	test(CLOCK_BOOTTIME, "boottime", false);

	nscheck();

	if (unshare(CLONE_NEWTIME))
		return pr_perror("Can't unshare() timens");

	nsfd = open("/proc/self/ns/time_for_children", O_RDONLY);
	if (nsfd < 0)
		return pr_perror("Can't open a time namespace");

	if (_settime(CLOCK_MONOTONIC, offset))
		return 1;
	if (_settime(CLOCK_BOOTTIME, offset))
		return 1;

	if (setns(nsfd, CLONE_NEWTIME))
		return pr_perror("setns");

	test(CLOCK_MONOTONIC, "monotonic", true);
	test(CLOCK_BOOTTIME, "boottime", true);

	ksft_exit_pass();
	return 0;
}
