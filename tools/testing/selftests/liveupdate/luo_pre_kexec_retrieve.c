// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2026, Google LLC.
 *
 * A selftest to validate pre-kexec file retrieval across a kexec reboot.
 * Stage 1 preserves memfds and retrieves them before kexec.
 * Stage 2 verifies that files retrieved before kexec are not present in
 * the session after kexec, while unretrieved files remain preserved.
 */

#include <libliveupdate.h>
#include <unistd.h>

#define SESSION_ONLY_RETRIEVED "pre-kexec-only-retrieved"
#define SESSION_MIXED "pre-kexec-mixed"

#define TOKEN_ONLY_RETRIEVED 0x1A
#define TOKEN_PRESERVED 0x2A
#define TOKEN_RETRIEVED 0x2B

#define DATA_ONLY_RETRIEVED "data retrieved before kexec (only file)"
#define DATA_PRESERVED "data preserved across kexec"
#define DATA_RETRIEVED "data retrieved before kexec (mixed session)"

#define STATE_SESSION_NAME "kexec_pre_retrieve_state"
#define STATE_MEMFD_TOKEN 997

/* Stage 1: Executed before the kexec reboot. */
static void run_stage_1(int luo_fd)
{
	int s_only_fd, s_mixed_fd, mfd;

	ksft_print_msg("[STAGE 1] Starting pre-kexec setup...\n");

	ksft_print_msg("[STAGE 1] Creating state file for next stage (2)...\n");
	create_state_file(luo_fd, STATE_SESSION_NAME, STATE_MEMFD_TOKEN, 2);

	/*
	 * Session 1: Preserve a single memfd and retrieve it before kexec.
	 */
	ksft_print_msg("[STAGE 1] Setting up session '%s'...\n",
		       SESSION_ONLY_RETRIEVED);
	s_only_fd = luo_create_session(luo_fd, SESSION_ONLY_RETRIEVED);
	if (s_only_fd < 0)
		fail_exit("luo_create_session for '%s'", SESSION_ONLY_RETRIEVED);

	if (create_and_preserve_memfd(s_only_fd, TOKEN_ONLY_RETRIEVED,
				      DATA_ONLY_RETRIEVED) < 0) {
		fail_exit("create_and_preserve_memfd for token %#x",
			  TOKEN_ONLY_RETRIEVED);
	}

	ksft_print_msg("[STAGE 1] Retrieving token %#x from '%s' before kexec...\n",
		       TOKEN_ONLY_RETRIEVED, SESSION_ONLY_RETRIEVED);
	mfd = restore_and_verify_memfd(s_only_fd, TOKEN_ONLY_RETRIEVED,
				       DATA_ONLY_RETRIEVED);
	if (mfd < 0)
		fail_exit("restore_and_verify_memfd pre-kexec for token %#x",
			  TOKEN_ONLY_RETRIEVED);
	close(mfd);

	/*
	 * Session 2: Preserve two memfds, retrieve one before kexec and
	 * leave the other preserved across kexec.
	 */
	ksft_print_msg("[STAGE 1] Setting up session '%s'...\n", SESSION_MIXED);
	s_mixed_fd = luo_create_session(luo_fd, SESSION_MIXED);
	if (s_mixed_fd < 0)
		fail_exit("luo_create_session for '%s'", SESSION_MIXED);

	if (create_and_preserve_memfd(s_mixed_fd, TOKEN_PRESERVED,
				      DATA_PRESERVED) < 0) {
		fail_exit("create_and_preserve_memfd for token %#x",
			  TOKEN_PRESERVED);
	}

	if (create_and_preserve_memfd(s_mixed_fd, TOKEN_RETRIEVED,
				      DATA_RETRIEVED) < 0) {
		fail_exit("create_and_preserve_memfd for token %#x",
			  TOKEN_RETRIEVED);
	}

	ksft_print_msg("[STAGE 1] Retrieving token %#x from '%s' before kexec...\n",
		       TOKEN_RETRIEVED, SESSION_MIXED);
	mfd = restore_and_verify_memfd(s_mixed_fd, TOKEN_RETRIEVED,
				       DATA_RETRIEVED);
	if (mfd < 0)
		fail_exit("restore_and_verify_memfd pre-kexec for token %#x",
			  TOKEN_RETRIEVED);
	close(mfd);

	close(luo_fd);
	daemonize_and_wait();
}

/* Stage 2: Executed after the kexec reboot. */
static void run_stage_2(int luo_fd, int state_session_fd)
{
	int s_only_fd, s_mixed_fd, mfd, stage, ret;

	ksft_print_msg("[STAGE 2] Starting post-kexec verification...\n");

	restore_and_read_stage(state_session_fd, STATE_MEMFD_TOKEN, &stage);
	if (stage != 2)
		fail_exit("Expected stage 2, but state file contains %d", stage);

	/*
	 * Verify Session 1: The session should exist, but TOKEN_ONLY_RETRIEVED
	 * must not be present (-ENOENT).
	 */
	ksft_print_msg("[STAGE 2] Retrieving session '%s'...\n",
		       SESSION_ONLY_RETRIEVED);
	s_only_fd = luo_retrieve_session(luo_fd, SESSION_ONLY_RETRIEVED);
	if (s_only_fd < 0)
		fail_exit("luo_retrieve_session for '%s'", SESSION_ONLY_RETRIEVED);

	ksft_print_msg("[STAGE 2] Verifying token %#x is NOT in '%s'...\n",
		       TOKEN_ONLY_RETRIEVED, SESSION_ONLY_RETRIEVED);
	ret = luo_session_retrieve_fd(s_only_fd, TOKEN_ONLY_RETRIEVED);
	if (ret != -ENOENT)
		fail_exit("Expected -ENOENT for pre-kexec retrieved token %#x, got %d",
			  TOKEN_ONLY_RETRIEVED, ret);

	/*
	 * Verify Session 2: TOKEN_PRESERVED must be present and valid, while
	 * TOKEN_RETRIEVED must not be present (-ENOENT).
	 */
	ksft_print_msg("[STAGE 2] Retrieving session '%s'...\n", SESSION_MIXED);
	s_mixed_fd = luo_retrieve_session(luo_fd, SESSION_MIXED);
	if (s_mixed_fd < 0)
		fail_exit("luo_retrieve_session for '%s'", SESSION_MIXED);

	ksft_print_msg("[STAGE 2] Verifying preserved token %#x in '%s'...\n",
		       TOKEN_PRESERVED, SESSION_MIXED);
	mfd = restore_and_verify_memfd(s_mixed_fd, TOKEN_PRESERVED,
				       DATA_PRESERVED);
	if (mfd < 0)
		fail_exit("restore_and_verify_memfd for token %#x",
			  TOKEN_PRESERVED);
	close(mfd);

	ksft_print_msg("[STAGE 2] Verifying pre-kexec retrieved token %#x is NOT in '%s'...\n",
		       TOKEN_RETRIEVED, SESSION_MIXED);
	ret = luo_session_retrieve_fd(s_mixed_fd, TOKEN_RETRIEVED);
	if (ret != -ENOENT)
		fail_exit("Expected -ENOENT for pre-kexec retrieved token %#x, got %d",
			  TOKEN_RETRIEVED, ret);

	ksft_print_msg("[STAGE 2] Finalizing test sessions...\n");
	if (luo_session_finish(s_only_fd) < 0)
		fail_exit("luo_session_finish for '%s'", SESSION_ONLY_RETRIEVED);
	close(s_only_fd);

	if (luo_session_finish(s_mixed_fd) < 0)
		fail_exit("luo_session_finish for '%s'", SESSION_MIXED);
	close(s_mixed_fd);

	ksft_print_msg("[STAGE 2] Finalizing state session...\n");
	if (luo_session_finish(state_session_fd) < 0)
		fail_exit("luo_session_finish for state session");
	close(state_session_fd);

	ksft_print_msg("\n--- PRE-KEXEC RETRIEVE KEXEC TEST PASSED ---\n");
}

int main(int argc, char *argv[])
{
	return luo_test(argc, argv, STATE_SESSION_NAME,
			run_stage_1, run_stage_2);
}
