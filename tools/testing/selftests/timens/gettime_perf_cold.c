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
#include <string.h>

#include "log.h"
#include "timens.h"

static __inline__ unsigned long long rdtsc(void)
{
	unsigned hi, lo;

	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long) lo) | (((unsigned long long)hi) << 32);
}

static void test(clock_t clockid, char *clockstr)
{
	struct timespec tp;
	long long s, e;

	s = rdtsc();
	clock_gettime(clockid, &tp);
	e = rdtsc();
	printf("%lld\n", e - s);
	return;
}

int main(int argc, char **argv)
{
	time_t offset = 10;
	int nsfd;

	if (argc == 1) {
		test(CLOCK_MONOTONIC, "monotonic");
		return 0;
	}
	nscheck();

	if (unshare(CLONE_NEWTIME))
		return pr_perror("Can't unshare() timens");

	nsfd = open("/proc/self/ns/time_for_children", O_RDONLY);
	if (nsfd < 0)
		return pr_perror("Can't open a time namespace");

	if (_settime(CLOCK_MONOTONIC, offset))
		return 1;

	if (setns(nsfd, CLONE_NEWTIME))
		return pr_perror("setns");

	test(CLOCK_MONOTONIC, "monotonic");
	return 0;
}
