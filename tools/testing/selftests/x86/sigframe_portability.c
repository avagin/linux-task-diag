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

#include "helpers.h"
#include "xstate.h"

/*
 * This test verifies the portability of the signal frame.
 * It simulates a scenario where a signal frame is created on a system with
 * fewer xstate features and restored on a system with more features.
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


static void handle_signal(int sig, siginfo_t *si, void *ucp)
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

int main(void)
{
	uint64_t v[4] = {0, 0, 0, 0};

	ksft_print_header();
	ksft_set_plan(1);

	check_avx_support();

	sethandler(SIGUSR1, handle_signal, 0);

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
		ksft_test_result_pass("YMM state restored correctly\n");
	else
		ksft_test_result_fail(
				"Got upper bits: 0x%lx 0x%lx (expected %lx %lx)\n",
			       v[2], v[3], TEST_YMMH_VAL, TEST_YMMH_VAL + 1);

	clearhandler(SIGUSR1);

	ksft_finished();
	return 0;
}
