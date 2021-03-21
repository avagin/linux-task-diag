// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <linux/elf.h>
#include <linux/unistd.h>

#include "../../kselftest.h"
#include "lib.h"

#define X7_TEST_VAL 0x686920776f726c64UL

static long test_syscall(long *x7)
{
	register long x0 __asm__("x0") = 0;
	register long *x1 __asm__("x1") = x7;
	register long x8 __asm__("x8") = 0x555;

	__asm__ (
		"ldr x7, [x1, 0]\n"
		"svc 0\n"
		"str x7, [x1, 0]\n"
			   : "=r"(x0)
			   : "r"(x0), "r"(x1), "r"(x8)
			   :
	);
	return x0;
}

static int child(void)
{
	long  val = X7_TEST_VAL;

	if (test_syscall(&val)) {
		ksft_print_msg("The test syscall failed\n");
		return 1;
	}
	if (val != X7_TEST_VAL) {
		ksft_print_msg("Unexpected x7: %lx\n", val);
		return 1;
	}

	if (test_syscall(&val)) {
		ksft_print_msg("The test syscall failed\n");
		return 1;
	}
	if (val != ~X7_TEST_VAL) {
		ksft_print_msg("Unexpected x7: %lx\n", val);
		return 1;
	}

	return 0;
}

#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU 31
#endif

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

	ksft_set_plan(4);

	pid = fork();
	if (pid == 0) {
		kill(getpid(), SIGSTOP);
		_exit(child());
	}
	if (pid < 0)
		return 1;

	if (ptrace_and_wait(pid, PTRACE_ATTACH, SIGSTOP))
		return 1;
	/* skip SIGSTOP */
	if (ptrace_and_wait(pid, PTRACE_CONT, SIGSTOP))
		return 1;

	/* Resume the child to the next system call. */
	if (ptrace_and_wait(pid, PTRACE_SYSCALL, SIGTRAP))
		return 1;

	/* Check that x7 is 0 on syscall-enter. */
	if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't get child registers");
	if (regs.orig_x7 != X7_TEST_VAL)
		return pr_fail("Unexpected orig_x7: %lx", regs.orig_x7);
	if (regs.r.regs[7] != 0)
		return pr_fail("Unexpected x7: %lx", regs.r.regs[7]);
	ksft_test_result_pass("x7: %llx\n", regs.r.regs[7]);

	if (ptrace_and_wait(pid, PTRACE_SYSCALL, SIGTRAP))
		return 1;

	/* Check that x7 is 1 on syscall-exit. */
	if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't get child registers");
	if (regs.r.regs[7] != 1)
		return pr_fail("Unexpected x7: %lx", regs.r.regs[7]);
	ksft_test_result_pass("x7: %llx\n", regs.r.regs[7]);

	/* Check that the child will not see a new value of x7. */
	regs.r.regs[0] = 0;
	regs.r.regs[7] = ~X7_TEST_VAL;
	if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't set child registers");

	/* Resume the child to the next system call. */
	if (ptrace_and_wait(pid, PTRACE_SYSEMU, SIGTRAP))
		return 1;

	/* Check that orig_x7 contains the actual value of x7. */
	if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't get child registers");
	if (regs.orig_x7 != X7_TEST_VAL)
		return pr_fail("Unexpected orig_x7: %lx", regs.orig_x7);
	ksft_test_result_pass("x7: %llx\n", regs.orig_x7);

	/* Check that the child will see a new value of x7. */
	regs.r.regs[0] = 0;
	regs.orig_x7 = ~X7_TEST_VAL;
	if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov))
		return pr_perror("Can't set child registers");

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

