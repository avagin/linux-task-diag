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

int run_test(int clockid)
{
	struct itimerspec new_value;
	struct timespec now;
	long long elapsed;
	int fd, i;

	if (clock_gettime(clockid, &now) == -1)
		handle_error("clock_gettime");

	for (i = 0; i < 2; i++) {
		printf("timerfd_settime: %d\n", clockid);
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
			handle_error("timerfd_create");

		if (timerfd_settime(fd, i == 1 ? TFD_TIMER_ABSTIME : 0, &new_value, NULL) == -1)
			handle_error("timerfd_settime");

		if (timerfd_gettime(fd, &new_value) == -1)
			handle_error("timerfd_gettime");

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
		handle_error("unshare");;

	if (clock_gettime(CLOCK_MONOTONIC, &tp))
		handle_error("clock_gettime");
	tp.tv_sec = 7 * 24 * 3600;
	if (clock_settime(CLOCK_MONOTONIC, &tp))
		handle_error("clock_settime");

	if (clock_gettime(CLOCK_BOOTTIME, &tp))
		handle_error("clock_gettime");
	tp.tv_sec += 9 * 24 * 3600;
	tp.tv_nsec = 0;
	if (clock_settime(CLOCK_BOOTTIME, &tp))
		handle_error("clock_settime");

	ret = 0;
	ret |= run_test(CLOCK_BOOTTIME);
	ret |= run_test(CLOCK_MONOTONIC);
	return ret;
}

