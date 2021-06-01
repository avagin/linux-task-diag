// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/prctl.h>
#include "asm/unistd.h"
#include <time.h>
#include <sys/mman.h>
#include <stdint.h>

#include "asm/process_vm_exec.h"
#include "linux/process_vm_exec.h"

#include "../kselftest.h"
#include "log.h"

#ifndef __NR_process_vm_exec
#define __NR_process_vm_exec 441
#endif

#define TEST_SYSCALL 123
#define TEST_SYSCALL_RET 456
#define TEST_MARKER 789
#define TEST_TIMEOUT 5
#define TEST_STACK_SIZE 65536

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

int marker;

static void guest(void)
{
	while (1)
		if (__syscall1(TEST_SYSCALL, marker) != TEST_SYSCALL_RET)
			abort();
}

int main(int argc, char **argv)
{
	struct process_vm_exec_context ctx = {
		.version = PROCESS_VM_EXEC_GOOGLE_V1,
	};
	struct timespec start, cur;
	int status, ret, i;
	pid_t pid;
	long sysnr;
	void *stack;

	ksft_set_plan(1);

	stack = mmap(NULL, TEST_STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	if (stack == MAP_FAILED)
		return pr_perror("mmap");

	pid  = fork();
	if (pid == 0) {
		marker = TEST_MARKER;
		prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
		kill(getpid(), SIGSTOP);
		/* unreachable */
		abort();
		return 0;
	}

#if defined(__x86_64__)
	ctx.sigctx.rip = (long)guest;
	ctx.sigctx.rsp = (long)stack + TEST_STACK_SIZE;
	ctx.sigctx.cs = 0x33;
#elif defined(__aarch64__)
	ctx.sigctx.pc = (long)guest;
	ctx.sigctx.pstate = 0x40001000;
        ctx.sigctx.sp = (long)stack + TEST_STACK_SIZE;
#endif

	sysnr = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		unsigned long long sigmask = 0xffffffff;
		siginfo_t siginfo;

		clock_gettime(CLOCK_MONOTONIC, &cur);
		if (start.tv_sec + TEST_TIMEOUT < cur.tv_sec ||
		    (start.tv_sec + TEST_TIMEOUT == cur.tv_sec &&
		     start.tv_nsec < cur.tv_nsec))
			break;

		ret = syscall(__NR_process_vm_exec, pid, &ctx, 0, &siginfo, &sigmask, 8);
#ifdef __DEBUG
		ksft_print_msg("ret %d signo %d sysno %d ip %lx\n",
			ret, siginfo.si_signo, siginfo.si_syscall, ctx.rip);
#endif
		if (ret != 0)
			pr_fail("unexpected return code: ret %d errno %d", ret, errno);
		if (siginfo.si_signo != SIGSYS)
			pr_fail("unexpected signal: %d", siginfo.si_signo);
		if (siginfo.si_syscall != TEST_SYSCALL)
			pr_fail("unexpected syscall: %d", siginfo.si_syscall);
#if defined(__x86_64__)
		ctx.sigctx.rax = TEST_SYSCALL_RET;
#elif defined(__aarch64__)
		ctx.sigctx.regs[0] = TEST_SYSCALL_RET;
#endif
		sysnr++;
	}
	ksft_test_result_pass("%ld ns/syscall\n", 1000000000 / sysnr);
	ksft_exit_pass();
	return 0;
}
