// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#include <linux/userfaultfd.h>
#include <sys/auxv.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#include "helpers.h"
#include "xstate.h"

#ifndef __x86_64__
# error This test is 64-bit only
#endif

/* err() exits and will not return */
#define fatal_error(msg, ...)	err(1, "[FAIL]\t" msg, ##__VA_ARGS__)

#define XFEATURE_MASK_XTILECFG	(1 << XFEATURE_XTILECFG)
#define XFEATURE_MASK_XTILEDATA	(1 << XFEATURE_XTILEDATA)
#define XFEATURE_MASK_XTILE	(XFEATURE_MASK_XTILECFG | XFEATURE_MASK_XTILEDATA)

struct xstate_info xtiledata;

/* The helpers for managing XSAVE buffer and tile states: */

struct xsave_buffer *stashed_xsave;

static void init_stashed_xsave(void)
{
	stashed_xsave = alloc_xbuf();
	if (!stashed_xsave)
		fatal_error("failed to allocate stashed_xsave\n");
	clear_xstate_header(stashed_xsave);
}

static void free_stashed_xsave(void)
{
	free(stashed_xsave);
}

/* Work around printf() being unsafe in signals: */
#define SIGNAL_BUF_LEN 1000
char signal_message_buffer[SIGNAL_BUF_LEN];
void sig_print(char *msg)
{
	int left = SIGNAL_BUF_LEN - strlen(signal_message_buffer) - 1;

	strncat(signal_message_buffer, msg, left);
}

static volatile bool noperm_signaled;
static int noperm_errs;
/*
 * Signal handler for when AMX is used but
 * permission has not been obtained.
 */
static void handle_noperm(int sig, siginfo_t *si, void *ctx_void)
{
	ucontext_t *ctx = (ucontext_t *)ctx_void;
	void *xbuf = ctx->uc_mcontext.fpregs;
	struct _fpx_sw_bytes *sw_bytes;
	uint64_t features;

	/* Reset the signal message buffer: */
	signal_message_buffer[0] = '\0';
	sig_print("\tAt SIGILL handler,\n");

	if (si->si_code != ILL_ILLOPC) {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid signal code.\n");
	} else {
		sig_print("[OK]\tValid signal code (ILL_ILLOPC).\n");
	}

	sw_bytes = get_fpx_sw_bytes(xbuf);
	/*
	 * Without permission, the signal XSAVE buffer should not
	 * have room for AMX register state (aka. xtiledata).
	 * Check that the size does not overlap with where xtiledata
	 * will reside.
	 *
	 * This also implies that no state components *PAST*
	 * XTILEDATA (features >=19) can be present in the buffer.
	 */
	if (sw_bytes->xstate_size <= xtiledata.xbuf_offset) {
		sig_print("[OK]\tValid xstate size\n");
	} else {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid xstate size\n");
	}

	features = get_fpx_sw_bytes_features(xbuf);
	/*
	 * Without permission, the XTILEDATA feature
	 * bit should not be set.
	 */
	if ((features & XFEATURE_MASK_XTILEDATA) == 0) {
		sig_print("[OK]\tValid xstate mask\n");
	} else {
		noperm_errs++;
		sig_print("[FAIL]\tInvalid xstate mask\n");
	}

	noperm_signaled = true;
	ctx->uc_mcontext.gregs[REG_RIP] += 3; /* Skip the faulting XRSTOR */
}

/* Return true if XRSTOR is successful; otherwise, false. */
static inline bool xrstor_safe(struct xsave_buffer *xbuf, uint64_t mask)
{
	noperm_signaled = false;
	xrstor(xbuf, mask);

	/* Print any messages produced by the signal code: */
	printf("%s", signal_message_buffer);
	/*
	 * Reset the buffer to make sure any future printing
	 * only outputs new messages:
	 */
	signal_message_buffer[0] = '\0';

	if (noperm_errs)
		fatal_error("saw %d errors in noperm signal handler\n", noperm_errs);

	return !noperm_signaled;
}

/*
 * Use XRSTOR to populate the XTILEDATA registers with
 * random data.
 *
 * Return true if successful; otherwise, false.
 */
static inline bool load_rand_tiledata(struct xsave_buffer *xbuf)
{
	clear_xstate_header(xbuf);
	set_xstatebv(xbuf, XFEATURE_MASK_XTILEDATA);
	set_rand_data(&xtiledata, xbuf);
	return xrstor_safe(xbuf, XFEATURE_MASK_XTILEDATA);
}

enum expected_result { FAIL_EXPECTED, SUCCESS_EXPECTED };

/* arch_prctl() and sigaltstack() test */

#define ARCH_GET_XCOMP_SUPP	0x1021
#define ARCH_GET_XCOMP_PERM	0x1022
#define ARCH_REQ_XCOMP_PERM	0x1023

static void req_xtiledata_perm(void)
{
	syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);
}

static void validate_req_xcomp_perm(enum expected_result exp)
{
	unsigned long bitmask, expected_bitmask;
	long rc;

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_PERM, &bitmask);
	if (rc) {
		fatal_error("prctl(ARCH_GET_XCOMP_PERM) error: %ld", rc);
	} else if (!(bitmask & XFEATURE_MASK_XTILECFG)) {
		fatal_error("ARCH_GET_XCOMP_PERM returns XFEATURE_XTILECFG off.");
	}

	rc = syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_PERM, XFEATURE_XTILEDATA);
	if (exp == FAIL_EXPECTED) {
		if (rc) {
			printf("[OK]\tARCH_REQ_XCOMP_PERM saw expected failure..\n");
			return;
		}

		fatal_error("ARCH_REQ_XCOMP_PERM saw unexpected success.\n");
	} else if (rc) {
		fatal_error("ARCH_REQ_XCOMP_PERM saw unexpected failure.\n");
	}

	expected_bitmask = bitmask | XFEATURE_MASK_XTILEDATA;

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_PERM, &bitmask);
	if (rc) {
		fatal_error("prctl(ARCH_GET_XCOMP_PERM) error: %ld", rc);
	} else if (bitmask != expected_bitmask) {
		fatal_error("ARCH_REQ_XCOMP_PERM set a wrong bitmask: %lx, expected: %lx.\n",
			    bitmask, expected_bitmask);
	} else {
		printf("\tARCH_REQ_XCOMP_PERM is successful.\n");
	}
}

static void validate_xcomp_perm(enum expected_result exp)
{
	bool load_success = load_rand_tiledata(stashed_xsave);

	if (exp == FAIL_EXPECTED) {
		if (load_success) {
			noperm_errs++;
			printf("[FAIL]\tLoad tiledata succeeded.\n");
		} else {
			printf("[OK]\tLoad tiledata failed.\n");
		}
	} else if (exp == SUCCESS_EXPECTED) {
		if (load_success) {
			printf("[OK]\tLoad tiledata succeeded.\n");
		} else {
			noperm_errs++;
			printf("[FAIL]\tLoad tiledata failed.\n");
		}
	}
}

#ifndef AT_MINSIGSTKSZ
#  define AT_MINSIGSTKSZ	51
#endif

static void *alloc_altstack(unsigned int size)
{
	void *altstack;

	altstack = mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

	if (altstack == MAP_FAILED)
		fatal_error("mmap() for altstack");

	return altstack;
}

static void setup_altstack(void *addr, unsigned long size, enum expected_result exp)
{
	stack_t ss;
	int rc;

	memset(&ss, 0, sizeof(ss));
	ss.ss_size = size;
	ss.ss_sp = addr;

	rc = sigaltstack(&ss, NULL);

	if (exp == FAIL_EXPECTED) {
		if (rc) {
			printf("[OK]\tsigaltstack() failed.\n");
		} else {
			fatal_error("sigaltstack() succeeded unexpectedly.\n");
		}
	} else if (rc) {
		fatal_error("sigaltstack()");
	}
}

static void test_dynamic_sigaltstack(void)
{
	unsigned int small_size, enough_size;
	unsigned long minsigstksz;
	void *altstack;

	minsigstksz = getauxval(AT_MINSIGSTKSZ);
	printf("\tAT_MINSIGSTKSZ = %lu\n", minsigstksz);
	/*
	 * getauxval() itself can return 0 for failure or
	 * success.  But, in this case, AT_MINSIGSTKSZ
	 * will always return a >=0 value if implemented.
	 * Just check for 0.
	 */
	if (minsigstksz == 0) {
		printf("no support for AT_MINSIGSTKSZ, skipping sigaltstack tests\n");
		return;
	}

	enough_size = minsigstksz * 2;

	altstack = alloc_altstack(enough_size);
	printf("\tAllocate memory for altstack (%u bytes).\n", enough_size);

	/*
	 * Try setup_altstack() with a size which can not fit
	 * XTILEDATA.  ARCH_REQ_XCOMP_PERM should fail.
	 */
	small_size = minsigstksz - xtiledata.size;
	printf("\tAfter sigaltstack() with small size (%u bytes).\n", small_size);
	setup_altstack(altstack, small_size, SUCCESS_EXPECTED);
	validate_req_xcomp_perm(FAIL_EXPECTED);

	/*
	 * Try setup_altstack() with a size derived from
	 * AT_MINSIGSTKSZ.  It should be more than large enough
	 * and thus ARCH_REQ_XCOMP_PERM should succeed.
	 */
	printf("\tAfter sigaltstack() with enough size (%u bytes).\n", enough_size);
	setup_altstack(altstack, enough_size, SUCCESS_EXPECTED);
	validate_req_xcomp_perm(SUCCESS_EXPECTED);

	/*
	 * Try to coerce setup_altstack() to again accept a
	 * too-small altstack.  This ensures that big-enough
	 * sigaltstacks can not shrink to a too-small value
	 * once XTILEDATA permission is established.
	 */
	printf("\tThen, sigaltstack() with small size (%u bytes).\n", small_size);
	setup_altstack(altstack, small_size, FAIL_EXPECTED);
}

static void test_dynamic_state(void)
{
	pid_t parent, child, grandchild;

	parent = fork();
	if (parent < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (parent > 0) {
		int status;
		/* fork() succeeded.  Now in the parent. */

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("arch_prctl test parent exit");
		return;
	}
	/* fork() succeeded.  Now in the child . */

	printf("[RUN]\tCheck ARCH_REQ_XCOMP_PERM around process fork() and sigaltack() test.\n");

	printf("\tFork a child.\n");
	child = fork();
	if (child < 0) {
		fatal_error("fork");
	} else if (child > 0) {
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("arch_prctl test child exit");
		_exit(0);
	}

	/*
	 * The permission request should fail without an
	 * XTILEDATA-compatible signal stack
	 */
	printf("\tTest XCOMP_PERM at child.\n");
	validate_xcomp_perm(FAIL_EXPECTED);

	/*
	 * Set up an XTILEDATA-compatible signal stack and
	 * also obtain permission to populate XTILEDATA.
	 */
	printf("\tTest dynamic sigaltstack at child:\n");
	test_dynamic_sigaltstack();

	/* Ensure that XTILEDATA can be populated. */
	printf("\tTest XCOMP_PERM again at child.\n");
	validate_xcomp_perm(SUCCESS_EXPECTED);

	printf("\tFork a grandchild.\n");
	grandchild = fork();
	if (grandchild < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (!grandchild) {
		/* fork() succeeded.  Now in the (grand)child. */
		printf("\tTest XCOMP_PERM at grandchild.\n");

		/*
		 * Ensure that the grandchild inherited
		 * permission and a compatible sigaltstack:
		 */
		validate_xcomp_perm(SUCCESS_EXPECTED);
	} else {
		int status;
		/* fork() succeeded.  Now in the parent. */

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test grandchild");
	}

	_exit(0);
}

static inline int __compare_tiledata_state(struct xsave_buffer *xbuf1, struct xsave_buffer *xbuf2)
{
	return memcmp(&xbuf1->bytes[xtiledata.xbuf_offset],
		      &xbuf2->bytes[xtiledata.xbuf_offset],
		      xtiledata.size);
}

/*
 * Save current register state and compare it to @xbuf1.'
 *
 * Returns false if @xbuf1 matches the registers.
 * Returns true  if @xbuf1 differs from the registers.
 */
static inline bool __validate_tiledata_regs(struct xsave_buffer *xbuf1)
{
	struct xsave_buffer *xbuf2;
	int ret;

	xbuf2 = alloc_xbuf();
	if (!xbuf2)
		fatal_error("failed to allocate XSAVE buffer\n");

	xsave(xbuf2, XFEATURE_MASK_XTILEDATA);
	ret = __compare_tiledata_state(xbuf1, xbuf2);

	free(xbuf2);

	if (ret == 0)
		return false;
	return true;
}

static inline void validate_tiledata_regs_changed(struct xsave_buffer *xbuf)
{
	int ret = __validate_tiledata_regs(xbuf);

	if (ret == 0)
		fatal_error("TILEDATA registers did not change");
}

/* tiledata inheritance test */

static void test_fork(void)
{
	pid_t child, grandchild;

	child = fork();
	if (child < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (child > 0) {
		/* fork() succeeded.  Now in the parent. */
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test child");
		return;
	}
	/* fork() succeeded.  Now in the child. */
	printf("[RUN]\tCheck tile data inheritance.\n\tBefore fork(), load tiledata\n");

	load_rand_tiledata(stashed_xsave);

	grandchild = fork();
	if (grandchild < 0) {
		/* fork() failed */
		fatal_error("fork");
	} else if (grandchild > 0) {
		/* fork() succeeded.  Still in the first child. */
		int status;

		wait(&status);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			fatal_error("fork test grand child");
		_exit(0);
	}
	/* fork() succeeded.  Now in the (grand)child. */

	/*
	 * TILEDATA registers are not preserved across fork().
	 * Ensure that their value has changed:
	 */
	validate_tiledata_regs_changed(stashed_xsave);

	_exit(0);
}

/*
 * Stale XTILEDATA after sigreturn test
 *
 * A task which has AMX permission but has not enabled XTILEDATA yet runs
 * with XFD[18] armed and without room for XTILEDATA in its fpstate.  If
 * such a task loses its FPU registers during rt_sigreturn, the restore
 * of the signal frame must still bring XTILEDATA into its init state.
 * Otherwise the tile registers keep the previous owner's data, which
 * becomes readable by the task once the first AMX instruction raises #NM
 * and the kernel clears XFD without reloading the registers.
 *
 * The test pins a "victim" process which keeps non-zero tile data in its
 * registers and an "attacker" process to the same CPU.  The attacker
 * points the fpstate of its signal frame at a userfaultfd-backed page,
 * so rt_sigreturn blocks and the victim runs in the meantime.  Once the
 * sigreturn completes, the attacker stores the tile registers to memory.
 * They must all be zero.
 */

#define TILE_NUM		8
#define TILE_ROWS		16
#define TILE_BYTES_PER_ROW	64
#define TILE_SIZE		(TILE_ROWS * TILE_BYTES_PER_ROW)

#define SIGFRAME_REGION_SIZE	(16 * 1024)
#define STALE_ITERATIONS	10

#define STALE_RESULT_CLEAN	0
#define STALE_RESULT_ERROR	1
#define STALE_RESULT_LEAKED	2

struct tile_config {
	uint8_t		palette_id;
	uint8_t		start_row;
	uint8_t		reserved[14];
	uint16_t	colsb[16];
	uint8_t		rows[16];
};

static struct xstate_info xtilecfg;

/* The tile pattern used by the victim. It is never all zero. */
static uint8_t tile_pattern(int tile, int byte)
{
	return 0x5a ^ (tile << 4) ^ (byte & 0xf) ^ 0x80;
}

static void fill_tile_config(struct tile_config *cfg)
{
	int i;

	memset(cfg, 0, sizeof(*cfg));
	cfg->palette_id = 1;
	for (i = 0; i < TILE_NUM; i++) {
		cfg->colsb[i] = TILE_BYTES_PER_ROW;
		cfg->rows[i] = TILE_ROWS;
	}
}

/* tilestored %tmmN, (%rax,%rdx,1) */
#define TILESTORED(n, base)						\
do {									\
	asm volatile(".byte 0xc4, 0xe2, 0x7a, 0x4b, " #n " * 8 + 4, 0x10"	\
		     : : "a" (base), "d" (TILE_BYTES_PER_ROW) : "memory");	\
} while (0)

static void store_all_tiles(uint8_t *buf)
{
	TILESTORED(0, buf + 0 * TILE_SIZE);
	TILESTORED(1, buf + 1 * TILE_SIZE);
	TILESTORED(2, buf + 2 * TILE_SIZE);
	TILESTORED(3, buf + 3 * TILE_SIZE);
	TILESTORED(4, buf + 4 * TILE_SIZE);
	TILESTORED(5, buf + 5 * TILE_SIZE);
	TILESTORED(6, buf + 6 * TILE_SIZE);
	TILESTORED(7, buf + 7 * TILE_SIZE);
}

/* Return the number of bytes in @buf which match the victim's pattern. */
static int count_pattern_bytes(uint8_t *buf)
{
	int tile, byte, matches = 0;

	for (tile = 0; tile < TILE_NUM; tile++) {
		for (byte = 0; byte < TILE_SIZE; byte++) {
			if (buf[tile * TILE_SIZE + byte] == tile_pattern(tile, byte))
				matches++;
		}
	}
	return matches;
}

static int count_nonzero_bytes(uint8_t *buf)
{
	int i, nonzero = 0;

	for (i = 0; i < TILE_NUM * TILE_SIZE; i++)
		nonzero += !!buf[i];
	return nonzero;
}

/*
 * Configure the tiles and load the pattern into them with a single XRSTOR.
 * The first XRSTOR of XTILEDATA raises #NM and enables it for the task.
 */
static void load_tile_pattern(void)
{
	struct xsave_buffer *xbuf;
	uint8_t *tiles;
	int tile, byte;

	xbuf = alloc_xbuf();
	if (!xbuf)
		fatal_error("failed to allocate XSAVE buffer");

	memset(xbuf, 0, get_xbuf_size());
	set_xstatebv(xbuf, XFEATURE_MASK_XTILE);
	fill_tile_config((struct tile_config *)&xbuf->bytes[xtilecfg.xbuf_offset]);

	tiles = (uint8_t *)&xbuf->bytes[xtiledata.xbuf_offset];
	for (tile = 0; tile < TILE_NUM; tile++) {
		for (byte = 0; byte < TILE_SIZE; byte++)
			tiles[tile * TILE_SIZE + byte] = tile_pattern(tile, byte);
	}

	xrstor(xbuf, XFEATURE_MASK_XTILE);
	free(xbuf);
}

/* Load only TILECFG. XTILEDATA is not in the requested feature bitmap. */
static void load_tile_config_only(void)
{
	struct xsave_buffer *xbuf;

	xbuf = alloc_xbuf();
	if (!xbuf)
		fatal_error("failed to allocate XSAVE buffer");

	memset(xbuf, 0, get_xbuf_size());
	set_xstatebv(xbuf, XFEATURE_MASK_XTILECFG);
	fill_tile_config((struct tile_config *)&xbuf->bytes[xtilecfg.xbuf_offset]);

	xrstor(xbuf, XFEATURE_MASK_XTILECFG);
	free(xbuf);
}

static void pin_to_cpu(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set))
		fatal_error("sched_setaffinity(%d)", cpu);
}

/*
 * The test detects stale tile data by reading it back after loading only
 * TILECFG.  Verify that this works on this CPU at all: load the pattern,
 * then TILECFG alone, and check that the pattern is still there.
 */
static bool tiledata_survives_tilecfg_load(void)
{
	uint8_t *buf;
	pid_t child;
	int status;

	child = fork();
	if (child < 0)
		fatal_error("fork");
	if (child == 0) {
		buf = aligned_alloc(64, TILE_NUM * TILE_SIZE);
		if (!buf)
			fatal_error("aligned_alloc");

		signal(SIGILL, SIG_DFL);
		load_tile_pattern();
		load_tile_config_only();
		memset(buf, 0, TILE_NUM * TILE_SIZE);
		store_all_tiles(buf);
		_exit(count_pattern_bytes(buf) == TILE_NUM * TILE_SIZE ? 0 : 1);
	}

	if (waitpid(child, &status, 0) != child)
		fatal_error("waitpid");
	if (!WIFEXITED(status))
		fatal_error("tile data check child died with status %x", status);
	return WEXITSTATUS(status) == 0;
}

static void victim_loop(int cpu, int ready_fd)
{
	char c = 0;

	signal(SIGILL, SIG_DFL);
	pin_to_cpu(cpu);
	load_tile_pattern();

	if (write(ready_fd, &c, 1) != 1)
		fatal_error("write");
	close(ready_fd);

	/* Spin in user space so that the tile data stays in the registers. */
	for (;;)
		asm volatile("pause");
}

/* Attacker state */
static int stale_uffd;
static void *stale_region;
static void *stale_staging;
static volatile bool stale_redirected;

static void *uffd_handler_thread(void *arg)
{
	struct timespec ts = { .tv_nsec = 50 * 1000 * 1000 };
	struct uffdio_copy copy = { };
	struct uffd_msg msg;

	if (read(stale_uffd, &msg, sizeof(msg)) != sizeof(msg))
		fatal_error("read(userfaultfd)");
	if (msg.event != UFFD_EVENT_PAGEFAULT)
		fatal_error("unexpected userfaultfd event %d", msg.event);

	/*
	 * The attacker is now blocked in rt_sigreturn and has lost its FPU
	 * registers. Give the victim time to run and load its tile data.
	 */
	nanosleep(&ts, NULL);

	copy.dst = (unsigned long)stale_region;
	copy.src = (unsigned long)stale_staging;
	copy.len = SIGFRAME_REGION_SIZE;
	copy.mode = 0;
	if (ioctl(stale_uffd, UFFDIO_COPY, &copy))
		fatal_error("UFFDIO_COPY");

	return NULL;
}

/*
 * Copy the signal frame's fpstate into the staging buffer, add a valid
 * tile configuration to it and make rt_sigreturn take the fpstate from the
 * userfaultfd-registered region, which is filled in from the staging
 * buffer by uffd_handler_thread().
 */
static void handle_stale_sigusr1(int sig, siginfo_t *si, void *ctx_void)
{
	ucontext_t *ctx = ctx_void;
	struct _fpx_sw_bytes *sw;
	uint64_t features;
	void *fp;

	fp = ctx->uc_mcontext.fpregs;
	sw = get_fpx_sw_bytes(fp);
	features = get_fpx_sw_bytes_features(fp);
	if (sw->magic1 != FP_XSTATE_MAGIC1 ||
	    sw->extended_size > SIGFRAME_REGION_SIZE ||
	    !(features & XFEATURE_MASK_XTILECFG) ||
	    (features & XFEATURE_MASK_XTILEDATA) ||
	    xtilecfg.xbuf_offset + sizeof(struct tile_config) > sw->xstate_size)
		return;

	memcpy(stale_staging, fp, sw->extended_size);
	fill_tile_config(stale_staging + xtilecfg.xbuf_offset);
	set_xstatebv(stale_staging,
		     get_xstatebv(stale_staging) | XFEATURE_MASK_XTILECFG);

	ctx->uc_mcontext.fpregs = stale_region;
	stale_redirected = true;
}

static int stale_tiledata_attacker(int cpu, int helper_cpu)
{
	struct uffdio_register reg = { };
	struct uffdio_api api = { };
	cpu_set_t helper_set;
	pthread_attr_t attr;
	pthread_t thread;
	uint8_t *buf;
	int nonzero;

	signal(SIGILL, SIG_DFL);
	pin_to_cpu(cpu);

	stale_uffd = syscall(__NR_userfaultfd, O_CLOEXEC);
	if (stale_uffd < 0)
		fatal_error("userfaultfd");
	api.api = UFFD_API;
	if (ioctl(stale_uffd, UFFDIO_API, &api))
		fatal_error("UFFDIO_API");

	stale_region = mmap(NULL, SIGFRAME_REGION_SIZE, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	stale_staging = mmap(NULL, SIGFRAME_REGION_SIZE, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stale_region == MAP_FAILED || stale_staging == MAP_FAILED)
		fatal_error("mmap");

	reg.range.start = (unsigned long)stale_region;
	reg.range.len = SIGFRAME_REGION_SIZE;
	reg.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(stale_uffd, UFFDIO_REGISTER, &reg))
		fatal_error("UFFDIO_REGISTER");

	buf = aligned_alloc(64, TILE_NUM * TILE_SIZE);
	if (!buf)
		fatal_error("aligned_alloc");
	memset(buf, 0, TILE_NUM * TILE_SIZE);

	/* Keep the helper off the shared CPU, it would reset the tiles there. */
	CPU_ZERO(&helper_set);
	CPU_SET(helper_cpu, &helper_set);
	pthread_attr_init(&attr);
	pthread_attr_setaffinity_np(&attr, sizeof(helper_set), &helper_set);
	if (pthread_create(&thread, &attr, uffd_handler_thread, NULL))
		fatal_error("pthread_create");

	sethandler(SIGUSR1, handle_stale_sigusr1, 0);
	raise(SIGUSR1);

	if (!stale_redirected) {
		printf("[FAIL]\tUnexpected signal frame layout\n");
		return STALE_RESULT_ERROR;
	}

	/*
	 * The tiles are configured by the sigreturn above. XTILEDATA is
	 * not enabled, so the first TILESTORED raises #NM, the kernel
	 * enables XTILEDATA and the instruction is restarted.
	 */
	store_all_tiles(buf);

	nonzero = count_nonzero_bytes(buf);
	if (nonzero) {
		printf("[FAIL]\tFound %d non-zero XTILEDATA bytes after sigreturn,\n"
		       "\t%d of them match the victim's data\n",
		       nonzero, count_pattern_bytes(buf));
		return STALE_RESULT_LEAKED;
	}

	pthread_join(thread, NULL);
	return STALE_RESULT_CLEAN;
}

static void test_sigreturn_stale_tiledata(void)
{
	int cpu = -1, helper_cpu = -1, i, uffd, leaks = 0;
	cpu_set_t set;
	int ready[2];
	pid_t victim;
	char c;

	printf("[RUN]\tCheck that sigreturn does not expose stale XTILEDATA.\n");

	xtilecfg = get_xstate_info(XFEATURE_XTILECFG);
	if (!xtilecfg.size || !xtilecfg.xbuf_offset)
		fatal_error("xstate cpuid: invalid tile config size/offset: %d/%d",
			    xtilecfg.size, xtilecfg.xbuf_offset);

	if (sched_getaffinity(0, sizeof(set), &set))
		fatal_error("sched_getaffinity");
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &set))
			continue;
		if (cpu < 0)
			cpu = i;
		else if (helper_cpu < 0)
			helper_cpu = i;
	}
	if (helper_cpu < 0) {
		printf("[SKIP]\tThe test needs at least two CPUs.\n");
		return;
	}

	/* Kernel-mode faults on userfaultfd regions need privileges. */
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC);
	if (uffd < 0) {
		printf("[SKIP]\tuserfaultfd is not available: %s\n", strerror(errno));
		return;
	}
	close(uffd);

	if (!tiledata_survives_tilecfg_load()) {
		printf("[SKIP]\tLoading TILECFG resets XTILEDATA on this CPU.\n");
		return;
	}

	if (pipe(ready))
		fatal_error("pipe");

	victim = fork();
	if (victim < 0)
		fatal_error("fork");
	if (victim == 0) {
		close(ready[0]);
		victim_loop(cpu, ready[1]);
	}

	close(ready[1]);
	if (read(ready[0], &c, 1) != 1)
		fatal_error("victim failed to start");
	close(ready[0]);

	for (i = 0; i < STALE_ITERATIONS; i++) {
		pid_t attacker;
		int status;

		fflush(stdout);
		attacker = fork();
		if (attacker < 0)
			fatal_error("fork");
		if (attacker == 0) {
			int ret = stale_tiledata_attacker(cpu, helper_cpu);

			/* _exit() does not flush stdio buffers. */
			fflush(stdout);
			_exit(ret);
		}

		if (waitpid(attacker, &status, 0) != attacker)
			fatal_error("waitpid");
		if (!WIFEXITED(status) ||
		    WEXITSTATUS(status) == STALE_RESULT_ERROR) {
			kill(victim, SIGKILL);
			waitpid(victim, NULL, 0);
			fatal_error("sigreturn test child failed, status %x", status);
		}
		if (WEXITSTATUS(status) == STALE_RESULT_LEAKED)
			leaks++;
	}

	kill(victim, SIGKILL);
	waitpid(victim, NULL, 0);

	if (leaks)
		fatal_error("stale XTILEDATA was exposed in %d of %d attempts",
			    leaks, STALE_ITERATIONS);

	printf("[OK]\tNo stale XTILEDATA after sigreturn in %d attempts.\n",
	       STALE_ITERATIONS);
}

int main(void)
{
	unsigned long features;
	long rc;

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_SUPP, &features);
	if (rc || (features & XFEATURE_MASK_XTILE) != XFEATURE_MASK_XTILE) {
		ksft_print_msg("no AMX support\n");
		return KSFT_SKIP;
	}

	xtiledata = get_xstate_info(XFEATURE_XTILEDATA);
	if (!xtiledata.size || !xtiledata.xbuf_offset) {
		fatal_error("xstate cpuid: invalid tile data size/offset: %d/%d",
			    xtiledata.size, xtiledata.xbuf_offset);
	}

	init_stashed_xsave();
	sethandler(SIGILL, handle_noperm, 0);

	test_dynamic_state();

	/* Request permission for the following tests */
	req_xtiledata_perm();

	test_fork();

	test_sigreturn_stale_tiledata();

	/*
	 * Perform generic xstate tests for context switching, ptrace,
	 * and signal.
	 */
	test_xstate(XFEATURE_XTILEDATA);

	clearhandler(SIGILL);
	free_stashed_xsave();

	return 0;
}
