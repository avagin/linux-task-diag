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

static __inline long __syscall1(long n, long a1)
{
        unsigned long ret;
        __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
        return ret;
}


int marker;

static void guest() {
	while (1) {
		__syscall1(666, marker);
	}
}

int main()
{
	struct user_regs_struct regs = {};
	struct timespec start, cur;
	pid_t pid;
	int status;
	long sysnr;

	pid  = fork();
	if (pid == 0) {
		marker = 5;
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
                return -1;
        }
	regs.rip = (long)guest;

	sysnr = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &cur);
		if (start.tv_sec + 5 < cur.tv_sec || (start.tv_sec + 5 == cur.tv_sec && start.tv_nsec < cur.tv_nsec))
			break;
		//printf("---> %d %llx\n", ret, regs.rip);
		if (ptrace(PTRACE_SETREGS, pid, NULL, &regs)) {
			return -1;
		}
		ptrace(PTRACE_SYSEMU, pid, 0, 0);
		waitpid(pid, NULL, 0);
		if (ptrace(PTRACE_GETREGS, pid, NULL, &regs)) {
			return -1;
		}
		//printf("---< ret %d rip %llx rax %lld rdi %llx: %m\n", ret, regs.rip, regs.rax, regs.orig_rax);
		regs.rax = 0;
	sysnr++;
	}
	printf("sysnr: %ld\n", sysnr);
	return 0;
}
