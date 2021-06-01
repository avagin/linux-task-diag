// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <asm/unistd.h>
#include <sys/ptrace.h>
#include <linux/elf.h>

#include "asm/process_vm_exec.h"
#include "linux/process_vm_exec.h"

#include "../kselftest.h"
#include "log.h"

#ifndef __NR_process_vm_exec
#define __NR_process_vm_exec 441
#endif

#define TEST_TIMEOUT 5
#define TEST_STACK_SIZE 65536

#define TEST_VAL 0xaabbccddee

unsigned long test_val;

#if defined(__x86_64__)
static inline void fault(long *addr, long val)
{
	__asm__ __volatile__ (
	"movq %%rcx, (%%rax)\n"
	:
	: "a"(addr), "c"(val)
	:);
}
#elif defined(__aarch64__)
static void fault(long *addr, long val)
{
	*addr = val;
}

static void fault(long *addr, long val) __attribute__((noinline));
#endif

int marker;

static void guest(void)
{
	long *addr = 0;

	while (1) {
		addr = (long *)(((long)addr + 1) % 8);
		fault(addr, 0);
		if (test_val != TEST_VAL)
			_exit(1);
	}
}

static long fault_addr;
#ifdef PROCESS_VM_EXEC_TEST_SIGNAL
static void segv(int signo, siginfo_t *info, void *data)
{
	fault_addr = (long)info->si_addr;
}
#endif

int main(char argc, char **argv)
{
	unsigned long long sigmask = 0xffffffff;
	struct process_vm_exec_context ctx = {
		.version = PROCESS_VM_EXEC_GOOGLE_V1,
	};
	siginfo_t siginfo;
	struct timespec start, cur;
	unsigned long addr;
	int status, ret, i;
	char *stack;
	pid_t pid;
	long faults;

	ksft_set_plan(1);

	stack = mmap(NULL, TEST_STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_SHARED | MAP_ANONYMOUS, 0, 0);
	if (stack == MAP_FAILED)
		return pr_perror("mmap");

	pid  = fork();
	if (pid == 0) {
		prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
		kill(getpid(), SIGSTOP);
		/* unreachable */
		abort();
		return 0;
	}

#ifdef PROCESS_VM_EXEC_TEST_SIGNAL
	{
		struct sigaction act = {
			.sa_sigaction = segv,
			.sa_flags = SA_SIGINFO,
		};
		if (sigaction(SIGSEGV, &act, NULL))
			return pr_perror("sigaction");
		sigmask = 0;
	}
#endif

#if defined(__x86_64__)
	ctx.sigctx.rip = (long)guest;
	ctx.sigctx.rsp = (long)stack + TEST_STACK_SIZE;
	ctx.sigctx.cs = 0x33;
#elif defined(__aarch64__)
	ctx.sigctx.pc = (long)guest;
	ctx.sigctx.pstate = 0x40001000;
        ctx.sigctx.sp = (long)stack + TEST_STACK_SIZE;
#endif

	faults = 0;
	addr = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		addr = (addr + 1) % 8;

		clock_gettime(CLOCK_MONOTONIC, &cur);
		if (start.tv_sec + TEST_TIMEOUT < cur.tv_sec ||
		    (start.tv_sec + TEST_TIMEOUT == cur.tv_sec &&
		     start.tv_nsec < cur.tv_nsec))
			break;

		ret = syscall(__NR_process_vm_exec, pid, &ctx, 0, &siginfo, &sigmask, 8);
#ifndef PROCESS_VM_EXEC_TEST_SIGNAL
		fault_addr = (long)siginfo.si_addr;
#endif
		if (fault_addr != addr % 8)
			return pr_fail("unexpected address: %lx", fault_addr);
#if defined(__x86_64__)
		if (addr % 8 != ctx.sigctx.rax)
			return pr_fail("unexpected address: %lx", ctx.sigctx.rax);
		ctx.sigctx.rax = (long)&test_val;
		ctx.sigctx.rcx = TEST_VAL;
#elif defined(__aarch64__)
		if (addr % 8 != ctx.sigctx.regs[0])
			return pr_fail("unexpected address: %lx", ctx.sigctx.regs[0]);
		ctx.sigctx.regs[0] = (long)&test_val;
		ctx.sigctx.regs[1] = TEST_VAL;
#endif
		faults++;
	}
	ksft_test_result_pass("%ld ns/signal\n", 1000000000 / faults);
	ksft_exit_pass();
	return 0;
}
