#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/user.h>
#include <sys/uio.h>
#include "asm/unistd.h"
#include <time.h>

#ifndef __NR_process_vm_exec
#define __NR_process_vm_exec 451
#endif

static __inline long __syscall1(long n, long a1)
{
        unsigned long ret;
        __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
        return ret;
}


int marker;

static void guest() {
	while (1) {
		if (__syscall1(123, marker) != 456)
			abort();
	}
}

int main()
{
	struct user_regs_struct regs = {};
	struct sigcontext _ctx = {};
	struct sigcontext *ctx = &_ctx;
	struct timespec start, cur;
	pid_t pid;
	int status, ret;
	long sysnr;

	pid  = fork();
	if (pid == 0) {
		marker = 789;
		kill(getpid(), SIGSTOP);
		return 0;
	}

	if (waitpid(pid, &status, WUNTRACED) != pid)
		return 1;
	if (ptrace(PTRACE_ATTACH, pid, 0, 0))
		return 1;
	if (wait(&status) != pid)
		return 1;

	if (ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_EXITKILL))
		return 1;

        if (ptrace(PTRACE_GETREGS, pid, NULL, &regs)) {
                return 1;
        }

	regs.rip = (long)guest;

	ctx->r8 = regs.r8;
	ctx->r9 = regs.r9;
	ctx->r10 = regs.r10;
	ctx->r11 = regs.r11;
	ctx->r12 = regs.r12;
	ctx->r13 = regs.r13;
	ctx->r14 = regs.r14;
	ctx->r15 = regs.r15;

	ctx->rdi = regs.rdi;
	ctx->rsi = regs.rsi;
	ctx->rbp = regs.rbp;
	ctx->rbx = regs.rbx;
	ctx->rdx = regs.rdx;
	ctx->rax = regs.rax;
	ctx->rcx = regs.rcx;
	ctx->rsp = regs.rsp;
	ctx->rip = regs.rip;
	ctx->eflags = regs.eflags;
	ctx->cs = regs.cs;
	ctx->gs = regs.gs;
	ctx->fs = regs.fs;

	sysnr = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &cur);
		if (start.tv_sec + 5 < cur.tv_sec ||
		    (start.tv_sec + 5 == cur.tv_sec && start.tv_nsec < cur.tv_nsec))
			break;
		ctx->rax = 456;
		ret = syscall(__NR_process_vm_exec, pid, ctx);
		if (ret != 0)
			abort();
		ctx->rax = 0;
		sysnr++;
	}
	printf("sysnr: %ld\n", sysnr);
	return 0;
}
