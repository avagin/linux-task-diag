#define _GNU_SOURCE
#include <sched.h>

#include <sys/timerfd.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#ifndef CLONE_NEWTIME
#define CLONE_NEWTIME   0x00001000	  /* New time namespace */
#endif

#define handle_error(msg) \
		do { perror(msg); exit(EXIT_FAILURE); } while (0)

static long long get_elapsed_time(int clockid, struct timespec *start)
{
	struct timespec curr;
	long long secs, nsecs;

	if (clock_gettime(clockid, &curr) == -1)
		handle_error("clock_gettime");

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
			handle_error("clock_gettime");


		if (i == 1) {
			now.tv_sec = start.tv_sec;
			now.tv_nsec = start.tv_nsec;
		}

		printf("clock_nanosleep: %d\n", clockid);
		now.tv_sec += 2;
		clock_nanosleep(clockid, i ? TIMER_ABSTIME : 0, &now, NULL);

		elapsed = get_elapsed_time(clockid, &start);
		if (elapsed < 1900 || elapsed > 2100) {
			printf("FAIL: elapsed %lld\n", elapsed);
			return 1;
		}
	}

	printf("PASS\n");

	return 0;
}

int main(int argc, char *argv[])
{
	struct timespec tp;
	int ret;

	if (unshare(CLONE_NEWTIME))
		handle_error("unshare");;

	if (clock_gettime(CLOCK_MONOTONIC, &tp))
		handle_error("clock_gettime");
	tp.tv_sec += 7 * 24 * 3600;
	if (clock_settime(CLOCK_MONOTONIC, &tp))
		handle_error("clock_settime");

	if (clock_gettime(CLOCK_BOOTTIME, &tp))
		handle_error("clock_gettime");
	tp.tv_sec += 9 * 24 * 3600;
	tp.tv_nsec = 0;
	if (clock_settime(CLOCK_BOOTTIME, &tp))
		handle_error("clock_settime");

	ret = 0;
	ret |= run_test(CLOCK_MONOTONIC);
	return ret;
}

