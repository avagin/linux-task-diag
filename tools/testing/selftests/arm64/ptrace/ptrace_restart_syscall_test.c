// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <linux/elf.h>
#include <linux/unistd.h>

#include "../../kselftest.h"
#include "lib.h"

static int child(int fd)
{
	char c;

	if (read(fd, &c, 1) != 1)
		return 1;

	return 0;
}

int main(int argc, void **argv)
{
	union {
		struct user_regs_struct r;
		struct {
			char __regs[272];
			unsigned long long orig_x0;
			unsigned long long orig_x7;
		};
	} regs = {};
	struct iovec iov = {
		.iov_base = &regs,
		.iov_len = sizeof(regs),
	};
	int status;
	pid_t pid;
	int p[2], fdzero;

	ksft_set_plan(3);

	if (pipe(p))
		return pr_perror("Can't create a pipe");
	fdzero = open("/dev/zero", O_RDONLY);
	if (fdzero < 0)
		return pr_perror("Can't open /dev/zero");

	pid = fork();
	if (pid == 0) {
		kill(getpid(), SIGSTOP);
		return child(p[0]);
	}
	if (pid < 0)
		return 1;

	if (ptrace(PTRACE_ATTACH, pid, 0, 0))
		return pr_perror("Can't attach to the child %d", pid);
	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("Can't wait for the child %d", pid);
	/* Skip SIGSTOP */
	if (ptrace_and_wait(pid, PTRACE_CONT, SIGSTOP))
		return 1;

	/* Resume the child to the next system call. */
	if (ptrace_and_wait(pid, PTRACE_SYSCALL, SIGTRAP))
		return 1;

	/* Send a signal to interrupt the system call. */
	kill(pid, SIGUSR1);

	/* Stop on syscall-exit. */
	if (ptrace_and_wait(pid, PTRACE_SYSCALL, SIGTRAP))
		return 1;

	if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't get child registers");
	if (regs.orig_x0 != p[0])
		return pr_fail("Unexpected x0: 0x%lx", regs.r.regs[0]);
	ksft_test_result_pass("orig_x0: 0x%llx\n", regs.orig_x0);

	/* Change orig_x0 that will be x0 for the restarted system call. */
	regs.orig_x0 = fdzero;
	if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't get child registers");

	/* Trap the signal and skip it. */
	if (ptrace_and_wait(pid, PTRACE_SYSCALL, SIGUSR1))
		return 1;

	/* Trap the restarted system call. */
	if (ptrace_and_wait(pid, PTRACE_SYSCALL, SIGTRAP))
		return 1;

	/* Check that the syscall is started with the right first argument. */
	if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't get child registers");
	if (regs.r.regs[0] != fdzero)
		return pr_fail("unexpected x0: %lx", regs.r.regs[0]);
	ksft_test_result_pass("x0: 0x%llx\n", regs.r.regs[0]);

	if (ptrace(PTRACE_CONT, pid, 0, 0))
		return pr_perror("Can't resume the child %d", pid);
	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("Can't wait for the child %d", pid);
	if (status != 0)
		return pr_fail("Child exited with code %d.", status);

	ksft_test_result_pass("The child exited with code 0.\n");
	ksft_exit_pass();
	return 0;
}

