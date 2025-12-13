// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <linux/prctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <elf.h>
#include <linux/auxvec.h>

#include "../kselftest.h"

static int find_msb(unsigned long v)
{
	return sizeof(v)*8 - __builtin_clzl(v) - 1;
}

int main(int argc, char *argv[])
{
	unsigned long auxv[1024], hwcap, new_hwcap, hwcap_idx;
	int size, hwcap_type = 0, hwcap_feature, count, status;
	char hwcap_str[32], hwcap_type_str[32];
	pid_t pid;

	if (argc > 1 && strcmp(argv[1], "verify") == 0) {
		unsigned long type = strtoul(argv[2], NULL, 16);
		unsigned long expected = strtoul(argv[3], NULL, 16);
		unsigned long hwcap = getauxval(type);

		if (hwcap != expected) {
			ksft_print_msg("HWCAP mismatch: type %lx, expected %lx, got %lx\n",
					type, expected, hwcap);
			return 1;
		}
		ksft_print_msg("HWCAP matched: %lx\n", hwcap);
		return 0;
	}

	ksft_print_header();
	ksft_set_plan(1);

	size = prctl(PR_GET_AUXV, auxv, sizeof(auxv), 0, 0);
	if (size == -1)
		ksft_exit_fail_perror("prctl(PR_GET_AUXV)");

	count = size / sizeof(unsigned long);

	/* Find the "latest" feature and try to mask it out. */
	for (int i = 0; i < count - 1; i += 2) {
		hwcap = auxv[i + 1];
		if (hwcap == 0)
			continue;
		switch (auxv[i]) {
		case AT_HWCAP4:
		case AT_HWCAP3:
		case AT_HWCAP2:
		case AT_HWCAP:
			hwcap_type = auxv[i];
			hwcap_feature = find_msb(hwcap);
			hwcap_idx = i + 1;
			break;
		default:
			continue;
		}
	}
	if (hwcap_type == 0)
		ksft_exit_skip("No features found, skipping test\n");
	hwcap = auxv[hwcap_idx];
	new_hwcap = hwcap ^ (1UL << hwcap_feature);
	auxv[hwcap_idx] = new_hwcap;

	if (prctl(PR_SET_MM, PR_SET_MM_AUXV, auxv, size, 0) < 0) {
		if (errno == EPERM)
			ksft_exit_skip("prctl(PR_SET_MM_AUXV) requires CAP_SYS_RESOURCE\n");
		ksft_exit_fail_perror("prctl(PR_SET_MM_AUXV)");
	}

	pid = fork();
	if (pid < 0)
		ksft_exit_fail_perror("fork");
	if (pid == 0) {
		char *new_argv[] = { argv[0], "verify", hwcap_type_str, hwcap_str, NULL };

		snprintf(hwcap_str, sizeof(hwcap_str), "%lx", new_hwcap);
		snprintf(hwcap_type_str, sizeof(hwcap_type_str), "%x", hwcap_type);

		execv(argv[0], new_argv);
		perror("execv");
		exit(1);
	}

	if (waitpid(pid, &status, 0) == -1)
		ksft_exit_fail_perror("waitpid");
	if (status != 0)
		ksft_exit_fail_msg("HWCAP inheritance failed (status %d)\n", status);

	ksft_test_result_pass("HWCAP inheritance succeeded\n");
	ksft_exit_pass();
	return 0;
}
