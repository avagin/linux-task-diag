// SPDX-License-Identifier: GPL-2.0

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <time.h>
#include <linux/elf.h>

#include "../kselftest.h"
#include "log.h"

#ifndef PTRACE_SYSEMU
#define PTRACE_SYSEMU            31
#endif

#if defined(__x86_64__)
static inline long __syscall1(long n, long a1)
{
	unsigned long ret;

	__asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");

	return ret;
}
#elif defined(__aarch64__)
#define __asm_syscall(...) do { \
        __asm__ __volatile__ ( "svc 0" \
        : "=r"(x0) : __VA_ARGS__ : "memory", "cc"); \
        return x0; \
        } while (0)


static inline long __syscall1(long n, long a)
{
        register long x8 __asm__("x8") = n;
        register long x0 __asm__("x0") = a;
        __asm_syscall("r"(x8), "0"(x0));
}
#endif

#define TEST_SYSCALL 444
#define TEST_SYSCALL_RET 555
#define TEST_MARKER 789
#define TEST_TIMEOUT 5

static int marker;

static void guest(void)
{
	while (1) {
		int ret;

		ret = __syscall1(TEST_SYSCALL, marker);
		if (ret != TEST_SYSCALL_RET)
			abort();
	}
}

int main(int argc, char **argv)
{
        struct user_regs_struct regs = {};
        struct iovec iov;
        iov.iov_base = &regs;
        iov.iov_len = sizeof(regs);


	struct timespec start, cur;
	int status;
	long sysnr;
	pid_t pid;

	ksft_set_plan(1);

	pid  = fork();
	if (pid == 0) {
		marker = TEST_MARKER;
		kill(getpid(), SIGSTOP);
		/* unreachable */
		abort();
		return 0;
	}

	if (waitpid(pid, &status, WUNTRACED) != pid)
		return pr_perror("waidpid");
	if (ptrace(PTRACE_ATTACH, pid, 0, 0))
		return pr_perror("PTRACE_ATTACH");
	if (wait(&status) != pid)
		return pr_perror("waidpid");
	if (ptrace(PTRACE_CONT, pid, 0, 0))
		return pr_perror("PTRACE_CONT");
	if (waitpid(pid, &status, 0) != pid)
		return pr_perror("waidpid");

	if (ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_EXITKILL))
		return pr_perror("PTRACE_SETOPTIONS");
	if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov) == -1)
		return pr_perror("PTRACE_GETREGS");
#if defined(__x86_64__)
	regs.rip = (long)guest;
#elif defined(__aarch64__)
	regs.pc = (long)guest;
#endif

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (sysnr = 0; ; sysnr++) {
		int status;

		clock_gettime(CLOCK_MONOTONIC, &cur);
		if (start.tv_sec + TEST_TIMEOUT < cur.tv_sec ||
		    (start.tv_sec + TEST_TIMEOUT == cur.tv_sec &&
		     start.tv_nsec < cur.tv_nsec))
			break;
		if (ptrace(PTRACE_SETREGSET, pid, (void*)NT_PRSTATUS, (void*)&iov) == -1)
			return pr_perror("PTRACE_GETREGS");
		if (ptrace(PTRACE_SYSEMU, pid, 0, 0))
			return pr_perror("PTRACE_SYSEMU");
		if (waitpid(pid, &status, 0) != pid)
			return pr_perror("waitpid");
		if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP)
			return pr_err("unexpected status: %d", status);
		if (ptrace(PTRACE_GETREGSET, pid, (void*)NT_PRSTATUS, (void*)&iov) == -1)
			return pr_perror("PTRACE_GETREGS");
#if defined(__x86_64__)
		if (regs.rdi != TEST_MARKER)
			return pr_err("unexpected marker: %d", regs.rdi);
		if (regs.orig_rax != TEST_SYSCALL)
			return pr_err("unexpected syscall: %d", regs.orig_rax);
		regs.rax = TEST_SYSCALL_RET;
#elif defined(__aarch64__)
		if (regs.regs[0] != TEST_MARKER)
			return pr_err("unexpected marker");
		if (regs.regs[8] != TEST_SYSCALL)
			return pr_err("unexpected syscall: %d", regs.regs[0]);
		regs.regs[0] = TEST_SYSCALL_RET;
#endif
	}
	ksft_test_result_pass("%ld ns/syscall\n", 1000000000 / sysnr);
	ksft_exit_pass();
	return 0;
}
