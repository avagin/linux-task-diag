// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/ucontext.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cpuid.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <asm/prctl.h>
#include <stddef.h>
#include <setjmp.h>

#include "helpers.h"
#include "xstate.h"

/*
 * This test verifies the FPU portability and consistency of the signal frame.
 *
 * - test_shrunk_xstate_size:
 *   Verifies that the kernel restores state from a frame with xstate_size
 *   shrunk to only include active features.
 *
 * - test_insufficient_xstate_size:
 *   Verifies that the kernel rejects a frame if xstate_size is too small for
 *   the features enabled in xfeatures.
 *
 * - test_larger_xstate_size:
 *   Verifies that the kernel restores state from a frame with xstate_size
 *   larger than the current task's size, if no unsupported features are active.
 *
 * - test_unsupported_xfeatures:
 *   Verifies that the kernel rejects a frame if it contains unsupported
 *   features in the xsave header.
 */

#define SIGFRAME_XSTATE_HDR_OFFSET	512
#define XSTATE_SSE_ONLY_SIZE	(SIGFRAME_XSTATE_HDR_OFFSET + XSAVE_HDR_SIZE)
#define XFEATURE_MASK_FPSSE	((1 << XFEATURE_FP) | (1 << XFEATURE_SSE))

static uint32_t ymm_offset;
static uint32_t xstate_size_ymm;

/*
 * Avoid using printf() in signal handlers as it is not
 * async-signal-safe.
 */
#define SIGNAL_BUF_LEN 1024
static char sig_err_buf[SIGNAL_BUF_LEN];

static void sig_print(const char *msg)
{
	int left = SIGNAL_BUF_LEN - strlen(sig_err_buf) - 1;

	strncat(sig_err_buf, msg, left);
}

static void check_avx_support(void)
{
	struct xstate_info xstate;
	unsigned long features;
	long rc;

	/*
	 * Check if the kernel supports AVX (XFEATURE_YMM).
	 * This also confirms that the OS has enabled XSAVE.
	 */
	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_SUPP, &features);
	if (rc != 0)
		ksft_exit_skip("ARCH_GET_XCOMP_SUPP not supported\n");

	if (!(features & (1 << XFEATURE_YMM)))
		ksft_exit_skip("AVX not supported by kernel/hardware\n");

	xstate = get_xstate_info(XFEATURE_YMM);
	if (!xstate.size)
		ksft_exit_skip("AVX not supported by hardware\n");

	ymm_offset = xstate.xbuf_offset;
	xstate_size_ymm = xstate.xbuf_offset + xstate.size;
}

#define TEST_YMMH_VAL (0x5656565656565656UL)

__attribute__((target("avx")))
static void read_ymm0(uint64_t *v)
{
	asm volatile ("vmovdqu %%ymm0, %0" : "=m"  (*(char (*)[32])v));
}

__attribute__((target("avx")))
static void write_ymm0(uint64_t *v)
{
	asm volatile ("vmovdqu %0, %%ymm0" : : "m"  (*(char (*)[32])v));
}

static void handle_shrunk_xstate_size(int sig, siginfo_t *si, void *ucp)
{
	ucontext_t *uc = ucp;
	void *fp = uc->uc_mcontext.fpregs;
	struct _fpx_sw_bytes *sw = get_fpx_sw_bytes(fp);
	struct xsave_buffer *xbuf;
	uint64_t xfeatures, *ymmh_p;

	if (sw->magic1 != FP_XSTATE_MAGIC1) {
		sig_print("magic1 is not valid\n");
		return;
	}

	xbuf = (struct xsave_buffer *)fp;

	/* Shrink the frame to just YMM size */
	sw->xstate_size = xstate_size_ymm;

	xfeatures = get_xstatebv(xbuf);
	xfeatures &= XFEATURE_MASK_FPSSE | (1 << XFEATURE_YMM);
	set_xstatebv(xbuf, xfeatures);
	/* Also update sw->xfeatures as the kernel relies on it */
	set_fpx_sw_bytes_features(fp, xfeatures);

	*(uint32_t *)(fp + sw->xstate_size) = FP_XSTATE_MAGIC2;

	ymmh_p = (uint64_t *)(fp + ymm_offset);
	ymmh_p[0] = TEST_YMMH_VAL;
	ymmh_p[1] = TEST_YMMH_VAL+1;

	/* clear everything after MAGIC2. */
	if (sw->xstate_size + 4 < sw->extended_size)
		memset(fp + sw->xstate_size + 4, 0, sw->extended_size - sw->xstate_size - 4);
}

static void test_shrunk_xstate_size(void)
{
	uint64_t v[4] = {0, 0, 0, 0};

	sig_err_buf[0] = 0;
	sethandler(SIGUSR1, handle_shrunk_xstate_size, 0);

	v[0] = 0x1111111111111111ULL;
	v[1] = 0x2222222222222222ULL;
	v[2] = 0x3333333333333333ULL;
	v[3] = 0x4444444444444444ULL;
	write_ymm0(v);

	raise(SIGUSR1);
	v[0] = v[1] = v[2] = v[3] = 0;
	read_ymm0(v);

	if (sig_err_buf[0])
		ksft_test_result_fail("%s\n", sig_err_buf);
	else if (v[2] == TEST_YMMH_VAL && v[3] == (TEST_YMMH_VAL + 1))
		ksft_test_result_pass("YMM state restored correctly from shrunk frame\n");
	else
		ksft_test_result_fail(
				"Got upper bits: 0x%lx 0x%lx (expected %lx %lx)\n",
			       v[2], v[3], TEST_YMMH_VAL, TEST_YMMH_VAL + 1);

	clearhandler(SIGUSR1);
}

static sigjmp_buf segv_jmpbuf;

static void handle_segv(int sig, siginfo_t *si, void *ucp)
{
	siglongjmp(segv_jmpbuf, 1);
}

static void handle_insufficient_xstate_size(int sig, siginfo_t *si, void *ucp)
{
	ucontext_t *uc = ucp;
	void *fp = uc->uc_mcontext.fpregs;
	struct _fpx_sw_bytes *sw = get_fpx_sw_bytes(fp);

	/* The origin frame contains an AVX state. */
	sw->xstate_size = XSTATE_SSE_ONLY_SIZE;

	*(uint32_t *)(fp + sw->xstate_size) = FP_XSTATE_MAGIC2;
}

static void test_insufficient_xstate_size(void)
{
	uint64_t v[4] = {0, 0, 0, 0};

	sig_err_buf[0] = 0;
	sethandler(SIGUSR1, handle_insufficient_xstate_size, 0);
	sethandler(SIGSEGV, handle_segv, 0);

	v[0] = 0x1111111111111111ULL;
	v[1] = 0x2222222222222222ULL;
	v[2] = 0x3333333333333333ULL;
	v[3] = 0x4444444444444444ULL;
	write_ymm0(v);

	if (sigsetjmp(segv_jmpbuf, 1) == 0) {
		raise(SIGUSR1);
		sig_print("Inconsistent size was NOT rejected\n");
	}

	clearhandler(SIGUSR1);
	clearhandler(SIGSEGV);

	if (sig_err_buf[0])
		ksft_test_result_fail("%s\n", sig_err_buf);
	else
		ksft_test_result_pass("Inconsistent size correctly rejected\n");

	clearhandler(SIGUSR1);
	clearhandler(SIGSEGV);
}

static char fpu_buffer[8192] __attribute__((aligned(64)));
#define UNSUPPORTED_XFEATURE (1ULL<<62)

static void __handle_larger_xstate_size(int sig, siginfo_t *si, void *ucp, bool mod_xhdr)
{
	ucontext_t *uc = ucp;
	void *fp = uc->uc_mcontext.fpregs;
	struct _fpx_sw_bytes *sw = get_fpx_sw_bytes(fp);
	size_t copy_size;
	uint64_t *ymmh_p, xfeatures;
	struct xsave_buffer *xbuf;

	if (sw->magic1 != FP_XSTATE_MAGIC1) {
		sig_print("magic1 is not valid\n");
		return;
	}

	copy_size = sw->xstate_size;
	if (copy_size > sizeof(fpu_buffer)) {
		sig_print("fpu_buffer is too small\n");
		return;
	}

	memset(fpu_buffer, 0, sizeof(fpu_buffer));
	memcpy(fpu_buffer, fp, copy_size);

	xbuf = (struct xsave_buffer *)fpu_buffer;
	sw = get_fpx_sw_bytes(fpu_buffer);

	sw->xstate_size += 64;
	sw->extended_size += 64;
	xfeatures = get_fpx_sw_bytes_features(xbuf);
	set_fpx_sw_bytes_features(fpu_buffer, xfeatures | UNSUPPORTED_XFEATURE);

	*(uint32_t *)((char *)fpu_buffer + sw->xstate_size) = FP_XSTATE_MAGIC2;

	if (mod_xhdr) {
		xfeatures = get_xstatebv(xbuf);
		set_xstatebv(xbuf, xfeatures | UNSUPPORTED_XFEATURE);
	}

	ymmh_p = (uint64_t *)(fpu_buffer + ymm_offset);
	ymmh_p[0] = TEST_YMMH_VAL;
	ymmh_p[1] = TEST_YMMH_VAL + 1;

	/* Update fpregs to point to the new buffer */
	uc->uc_mcontext.fpregs = (fpregset_t)fpu_buffer;
}

static void handle_larger_xstate_size(int sig, siginfo_t *si, void *ucp)
{
	__handle_larger_xstate_size(sig, si, ucp, false);
}

static void test_larger_xstate_size(void)
{
	uint64_t v[4] = {0, 0, 0, 0};

	sig_err_buf[0] = 0;
	sethandler(SIGUSR1, handle_larger_xstate_size, 0);

	v[0] = 0x1111111111111111ULL;
	v[1] = 0x2222222222222222ULL;
	v[2] = 0x3333333333333333ULL;
	v[3] = 0x4444444444444444ULL;
	write_ymm0(v);

	raise(SIGUSR1);
	v[0] = v[1] = v[2] = v[3] = 0;
	read_ymm0(v);

	if (sig_err_buf[0])
		ksft_test_result_fail("%s\n", sig_err_buf);
	else if (v[2] == TEST_YMMH_VAL && v[3] == TEST_YMMH_VAL + 1)
		ksft_test_result_pass("YMM state restored correctly with larger xstate_size\n");
	else
		ksft_test_result_fail(
				"Got upper bits: 0x%lx 0x%lx (expected %lx %lx)\n",
			       v[2], v[3], TEST_YMMH_VAL, TEST_YMMH_VAL + 1);

	clearhandler(SIGUSR1);
}

static void handle_unsupported_xfeatures(int sig, siginfo_t *si, void *ucp)
{
	__handle_larger_xstate_size(sig, si, ucp, true);
}

static void test_unsupported_xfeatures(void)
{
	uint64_t v[4] = {0, 0, 0, 0};

	sig_err_buf[0] = 0;

	sethandler(SIGUSR1, handle_unsupported_xfeatures, 0);
	sethandler(SIGSEGV, handle_segv, 0);

	v[0] = 0x1111111111111111ULL;
	v[1] = 0x2222222222222222ULL;
	v[2] = 0x3333333333333333ULL;
	v[3] = 0x4444444444444444ULL;
	write_ymm0(v);

	if (sigsetjmp(segv_jmpbuf, 1) == 0) {
		raise(SIGUSR1);
		sig_print("raise(SIGUSR1) returned (expected SIGSEGV)\n");
	}

	clearhandler(SIGUSR1);
	clearhandler(SIGSEGV);

	if (sig_err_buf[0])
		ksft_test_result_fail("%s\n", sig_err_buf);
	else
		ksft_test_result_pass("Unsupported feature in xsave header triggered SIGSEGV\n");
}

int main(void)
{
	ksft_print_header();
	ksft_set_plan(4);

	check_avx_support();

	test_shrunk_xstate_size();
	test_insufficient_xstate_size();
	test_larger_xstate_size();
	test_unsupported_xfeatures();

	ksft_finished();
	return 0;
}
