// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <dlfcn.h>
#include <signal.h>

#include "log.h"
#include "timens.h"

#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64

typedef int (*vgettime_t)(clockid_t, struct timespec *);

vgettime_t vdso_clock_gettime;

static void fill_function_pointers(void)
{
	void *vdso = dlopen("linux-vdso.so.1",
			    RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso)
		vdso = dlopen("linux-gate.so.1",
			      RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
	if (!vdso) {
		pr_err("[WARN]\tfailed to find vDSO\n");
		return;
	}

	vdso_clock_gettime = (vgettime_t)dlsym(vdso, "__vdso_clock_gettime");
	if (!vdso_clock_gettime)
		pr_err("Warning: failed to find clock_gettime in vDSO\n");

}

static inline __attribute__((always_inline)) unsigned long long rdtsc(void)
{
	unsigned int hi, lo;

	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((unsigned long long) lo) | (((unsigned long long)hi) << 32);
}

static inline __attribute__((always_inline)) void test(clock_t clockid, char *clockstr)
{
	struct timespec tp;
	long long s, e;

	s = rdtsc();
	vdso_clock_gettime(clockid, &tp);
	e = rdtsc();
	printf("%lld\n", e - s);
}

static inline void clflush(volatile void *__p)
{
	asm volatile("clflush %0" : "+m"(*(volatile char *)__p));
}

void *pg_addr;
void sigh(int sig)
{
	void *addr;

	addr = mmap(pg_addr, PAGE_SIZE, PROT_READ,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (addr != pg_addr) {
		pr_perror("Unable to map %lx", (long) pg_addr);
		exit(1);
	}
}

int main(int argc, char **argv)
{
	time_t offset = 10;
	void *vdso_start = 0, *vdso_end = 0;
	void *vvar_start = 0, *vvar_end = 0;
	char buf[PAGE_SIZE];
	int nsfd, i;
	FILE *maps;

	fill_function_pointers();
	if (argc == 1)
		goto out;
	nscheck();

	if (unshare_timens())
		return 1;

	nsfd = open("/proc/self/ns/time_for_children", O_RDONLY);
	if (nsfd < 0)
		return pr_perror("Can't open a time namespace");

	if (_settime(CLOCK_MONOTONIC, offset))
		return 1;

	if (setns(nsfd, CLONE_NEWTIME))
		return pr_perror("setns");

out:
	maps = fopen("/proc/self/maps", "r");
	if (!maps) {
		pr_perror("Unable to open /proc/self/maps");
		return 1;
	}

	while (fgets(buf, sizeof(buf), maps)) {
		unsigned long start, end;
		char tail[PAGE_SIZE];
		int r;

		r = sscanf(buf, "%lx-%lx %*s %*s %*s %*s %s\n", &start, &end, tail);

		if (r < 3)
			continue;

		if (strcmp(tail, "[vdso]") == 0) {
			vdso_start = (void *)start;
			vdso_end = (void *)end;
		}
		if (strcmp(tail, "[vvar]") == 0) {
			vvar_start = (void *)start;
			vvar_end = (void *)end;
		}
	}
	if (!vvar_start || !vdso_start) {
		pr_err("Unable to find vdso\n");
		return 1;
	}

	/* Map zero pages instead of unreadable vdso pages. */
	signal(SIGSEGV, sigh);
	signal(SIGBUS, sigh);
	for (pg_addr = vdso_start; pg_addr < vdso_end; pg_addr += PAGE_SIZE)
		buf[0] += *(char *)pg_addr;
	for (pg_addr = vvar_start; pg_addr < vvar_end; pg_addr += PAGE_SIZE)
		buf[0] += *(char *)pg_addr;
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);

	for (i = 0; i < 10240; i++) {
		void *p;

		for (p = vdso_start; p < vdso_end; p += CACHE_LINE_SIZE)
			clflush(p);
		for (p = vvar_start; p < vvar_end; p += CACHE_LINE_SIZE)
			clflush(p);
		test(CLOCK_MONOTONIC, "monotonic");
	}
	return 0;
}
