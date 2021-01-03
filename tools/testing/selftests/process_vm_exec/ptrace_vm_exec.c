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


static void guest() {
	while (1) {
		int ret;

		ret = __syscall1(444, 333);
		if (ret != 555) {
			/* abort */
			*((int *)0xdea) = 0xd;
		}
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
		kill(getpid(), SIGSTOP);
		/* unreachable */
		*((int *)0xdea) = 0xd;
		return 0;
	}

	if (waitpid(pid, &status, WUNTRACED) != pid)
		return 1;
	if (ptrace(PTRACE_ATTACH, pid, 0, 0))
		return 1;
	if (wait(&status) != pid)
		return 1;
	if (ptrace(PTRACE_CONT, pid, 0, 0))
		return 1;
	if (wait(&status) != pid)
		return 1;

	if (ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_EXITKILL))
		return 1;
        if (ptrace(PTRACE_GETREGS, pid, NULL, &regs)) {
                return -1;
        }
	regs.rip = (long)guest;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (sysnr = 0;;sysnr++) {
		int status;

		clock_gettime(CLOCK_MONOTONIC, &cur);
		if (start.tv_sec + 5 < cur.tv_sec ||
		    (start.tv_sec + 5 == cur.tv_sec && start.tv_nsec < cur.tv_nsec))
			break;
		if (ptrace(PTRACE_SETREGS, pid, NULL, &regs)) {
			return 1;
		}
		if (ptrace(PTRACE_SYSEMU, pid, 0, 0))
			return 1;
		if (waitpid(pid, &status, 0) != pid)
			return 1;
		if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP)
			return 1;
		if (ptrace(PTRACE_GETREGS, pid, NULL, &regs)) {
			return 1;
		}
		if (regs.orig_rax != 444)
			return 1;
		regs.rax = 555;
	}
	printf("sysnr: %ld\n", sysnr);
	return 0;
}
