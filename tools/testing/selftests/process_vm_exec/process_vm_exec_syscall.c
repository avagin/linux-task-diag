// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <asm/unistd.h>

#include "asm/process_vm_exec.h"
#include "linux/process_vm_exec.h"

#include "../kselftest.h"
#include "log.h"

#ifndef __NR_process_vm_exec
#define __NR_process_vm_exec 441
#endif

#ifndef PROCESS_VM_EXEC_SYSCALL
#define PROCESS_VM_EXEC_SYSCALL 0x1
#endif

#define TEST_VAL 0x1e511e51

int test_val = TEST_VAL;

int main(int argc, char **argv)
{
	struct process_vm_exec_context ctx = {
		.version = PROCESS_VM_EXEC_GOOGLE_V1,
	};
	unsigned long long sigmask;
	int ret, p[2], val;
	siginfo_t siginfo = {};
	pid_t pid;

	ksft_set_plan(1);

	pid  = fork();
	if (pid < 0)
		return pr_perror("fork");
	if (pid == 0) {
		prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
		kill(getpid(), SIGSTOP);
		return 0;
	}

	test_val = 0;
	if (pipe(p))
		return pr_perror("pipe");

#if defined(__x86_64__)
	ctx.sigctx.rax = __NR_write;
	ctx.sigctx.rdi = p[1];
	ctx.sigctx.rsi = (unsigned long) &test_val;
	ctx.sigctx.rdx = sizeof(test_val);
	ctx.sigctx.r10 = 0;
	ctx.sigctx.r8 = 0;
	ctx.sigctx.r9 = 0;
#elif defined(__aarch64__)
	ctx.sigctx.regs[8] = __NR_write;
	ctx.sigctx.regs[0] = p[1];
	ctx.sigctx.regs[1] = (unsigned long) &test_val;
	ctx.sigctx.regs[2] = sizeof(test_val);
	ctx.sigctx.regs[3] = 0;
	ctx.sigctx.regs[4] = 0;
	ctx.sigctx.regs[5] = 0;
#endif
	sigmask = 0xffffffff;
	ret = syscall(__NR_process_vm_exec, pid, &ctx, PROCESS_VM_EXEC_SYSCALL,
		      &siginfo, &sigmask, 8);
	if (ret != 0)
		return pr_perror("process_vm_exec");
	if (siginfo.si_signo != 0)
		return pr_fail("unexpected signal: %d", siginfo.si_signo);
#if defined(__x86_64__)
	if (ctx.sigctx.rax != sizeof(test_val))
		pr_fail("unexpected rax: %lx", ctx.sigctx.rax);
#elif defined(__aarch64__)
	if (ctx.sigctx.regs[0] != sizeof(test_val))
		pr_fail("unexpected r0: %lx", ctx.sigctx.regs[0]);
#endif
	if (kill(pid, SIGKILL))
		return pr_perror("kill");
	if (wait(NULL) != pid)
		return pr_perror("kill");
	if (read(p[0], &val, sizeof(val)) != sizeof(val))
		pr_perror("read");
	if (val != TEST_VAL)
		pr_fail("unexpected data: %x", val);
	ksft_test_result_pass("process_vm_exec(..., PROCESS_VM_EXEC_SYSCALL, ...) \n");
	ksft_exit_pass();
	return 0;
}
