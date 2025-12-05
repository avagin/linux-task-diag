// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <linux/limits.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../kselftest.h"
#include "cgroup_util.h"

/*
 * This test checks that misc.mask works correctly.
 */
static int test_misc_mask(const char *root)
{
	int ret = KSFT_FAIL;
	char *cg_misc, *cg_misc_sub = NULL;

	cg_misc = cg_name(root, "misc_test");
	if (!cg_misc)
		goto cleanup;

	cg_misc_sub = cg_name(root, "misc_test/sub");
	if (!cg_misc_sub)
		goto cleanup;

	if (cg_create(cg_misc))
		goto cleanup;

	if (cg_read_strcmp(cg_misc, "misc.mask",
			   "AT_HWCAP\t0x00000000000000\t0x00000000000000\n"))
		goto cleanup;

	if (cg_write(cg_misc, "misc.mask", "AT_HWCAP 0xf0000000000000"))
		goto cleanup;

	if (cg_read_strcmp(cg_misc, "misc.mask",
			   "AT_HWCAP\t0xf0000000000000\t0xf0000000000000\n"))
		goto cleanup;

	if (cg_write(cg_misc, "cgroup.subtree_control", "+misc"))
		goto cleanup;

	if (cg_create(cg_misc_sub))
		goto cleanup;

	if (cg_read_strcmp(cg_misc_sub, "misc.mask",
			   "AT_HWCAP\t0x00000000000000\t0xf0000000000000\n"))
		goto cleanup;

	if (cg_write(cg_misc_sub, "misc.mask", "AT_HWCAP 0x01000000000000"))
		goto cleanup;

	if (cg_read_strcmp(cg_misc_sub, "misc.mask",
			   "AT_HWCAP\t0x01000000000000\t0xf1000000000000\n"))
		goto cleanup;

	ret = KSFT_PASS;

cleanup:
	cg_enter_current(root);
	cg_destroy(cg_misc_sub);
	cg_destroy(cg_misc);
	free(cg_misc);
	free(cg_misc_sub);

	return ret;
}

#define T(x) { x, #x }
struct misc_test {
	int (*fn)(const char *root);
	const char *name;
} tests[] = {
	T(test_misc_mask),
};
#undef T

int main(int argc, char **argv)
{
	char root[PATH_MAX];

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(tests));
	if (cg_find_unified_root(root, sizeof(root), NULL))
		ksft_exit_skip("cgroup v2 isn't mounted\n");

	/*
	 * Check that misc controller is available:
	 * misc is listed in cgroup.controllers
	 */
	if (cg_read_strstr(root, "cgroup.controllers", "misc"))
		ksft_exit_skip("misc controller isn't available\n");

	if (cg_read_strstr(root, "cgroup.subtree_control", "misc"))
		if (cg_write(root, "cgroup.subtree_control", "+misc"))
			ksft_exit_skip("Failed to set misc controller\n");

	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		switch (tests[i].fn(root)) {
		case KSFT_PASS:
			ksft_test_result_pass("%s\n", tests[i].name);
			break;
		case KSFT_SKIP:
			ksft_test_result_skip("%s\n", tests[i].name);
			break;
		default:
			ksft_test_result_fail("%s\n", tests[i].name);
			break;
		}
	}

	ksft_finished();
}
