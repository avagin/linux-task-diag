// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>

#include <sys/timerfd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "log.h"
#include "timens.h"

int run_test(int clockid, struct timespec now)
{
	struct itimerspec new_value;
	long long elapsed;
	int fd, i;

	if (clock_gettime(clockid, &now))
		return pr_perror("clock_gettime");

	for (i = 0; i < 2; i++) {
		int flags = 0;

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
			ksft_test_result_fail("clockid: %d elapsed: %lld\n",
					      clockid, elapsed);
			return 1;
		}

		close(fd);
	}

	ksft_test_result_pass("clockid=%d\n", clockid);

	return 0;
}

int main(int argc, char *argv[])
{
	int ret, status, len, fd;
	char buf[4096];
	pid_t pid;
	struct timespec btime_now, mtime_now;

	nscheck();

	clock_gettime(CLOCK_MONOTONIC, &mtime_now);
	clock_gettime(CLOCK_BOOTTIME, &btime_now);

	if (unshare(CLONE_NEWTIME))
		return pr_perror("unshare");

	len = snprintf(buf, sizeof(buf), "%d %d 0\n%d %d 0",
			CLOCK_MONOTONIC, 70 * 24 * 3600,
			CLOCK_BOOTTIME, 9 * 24 * 3600);
	fd = open("/proc/self/timens_offsets", O_WRONLY);
	if (fd < 0)
		return pr_perror("/proc/self/timens_offsets");

	if (write(fd, buf, len) != len)
		return pr_perror("/proc/self/timens_offsets");

	close(fd);
	mtime_now.tv_sec += 70 * 24 * 3600;
	btime_now.tv_sec += 9 * 24 * 3600;

	pid = fork();
	if (pid < 0)
		return pr_perror("Unable to fork");
	if (pid == 0) {
		ret = 0;
		ret |= run_test(CLOCK_BOOTTIME, btime_now);
		ret |= run_test(CLOCK_MONOTONIC, mtime_now);

		if (ret)
			ksft_exit_fail();
		ksft_exit_pass();
		return ret;
	}

	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("Unable to wait the child process");

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	return 1;
}

