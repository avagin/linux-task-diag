// SPDX-License-Identifier: GPL-2.0-only
#ifndef __PTRACE_TEST_LOG_H
#define __PTRACE_TEST_LOG_H

#define pr_p(func, fmt, ...) func("%s:%d: " fmt ": %m", \
				  __func__, __LINE__, ##__VA_ARGS__)

#define pr_err(fmt, ...)						\
	({								\
		ksft_test_result_error(fmt "\n", ##__VA_ARGS__);	\
		-1;							\
	})

#define pr_fail(fmt, ...)					\
	({							\
		ksft_test_result_fail(fmt "\n", ##__VA_ARGS__);	\
		-1;						\
	})

#define pr_perror(fmt, ...)	pr_p(pr_err, fmt, ##__VA_ARGS__)

static inline int ptrace_and_wait(pid_t pid, int cmd, int sig)
{
	int status;

	/* Stop on syscall-exit. */
	if (ptrace(cmd, pid, 0, 0))
		return pr_perror("Can't resume the child %d", pid);
	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("Can't wait for the child %d", pid);
	if (!WIFSTOPPED(status) || WSTOPSIG(status) != sig)
		return pr_err("Unexpected status: %x", status);
	return 0;
}

#endif
