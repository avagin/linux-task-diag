// SPDX-License-Identifier: GPL-2.0

#include <asm/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test_progs.h"
#include "cgroup_helpers.h"
#include "cpuid_cgroup.skel.h"

#define TEST_CGROUP "/test-bpf-cpuid-cgroup/"

#ifdef __x86_64__
static void run_cpuid(__u32 eax_in, __u32 ecx_in,
		      __u32 *eax, __u32 *ebx, __u32 *ecx, __u32 *edx)
{
	asm volatile("cpuid"
		     : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
		     : "a" (eax_in), "c" (ecx_in));
}

void test_cgroup_cpuid(void)
{
	__u32 orig_eax0, orig_ebx0, orig_ecx0, orig_edx0;
	__u32 eax, ebx, ecx, edx;
	struct bpf_link *link1 = NULL, *link2 = NULL;
	struct cpuid_cgroup *skel = NULL;
	int cgroup_fd, status;
	pid_t pid;

	run_cpuid(0, 0, &orig_eax0, &orig_ebx0, &orig_ecx0, &orig_edx0);

	cgroup_fd = cgroup_setup_and_join(TEST_CGROUP);
	if (!ASSERT_OK_FD(cgroup_fd, "cgroup_setup_and_join"))
		return;

	skel = cpuid_cgroup__open_and_load();
	if (!ASSERT_OK_PTR(skel, "cpuid_cgroup__open_and_load"))
		goto cleanup_cgroup;

	link1 = bpf_program__attach_cgroup(skel->progs.cpuid_prog1, cgroup_fd);
	if (libbpf_get_error(link1) == -EOPNOTSUPP) {
		test__skip();
		link1 = NULL;
		goto cleanup_progs;
	}
	if (!ASSERT_OK_PTR(link1, "attach_prog1"))
		goto cleanup_progs;

	if (test__start_subtest("virtualize")) {
		run_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
		ASSERT_EQ(eax, orig_eax0, "leaf0_eax");
		ASSERT_EQ(ebx, 0x46504265, "leaf0_ebx");
		ASSERT_EQ(edx, 0x54534554, "leaf0_edx");
		ASSERT_EQ(ecx, 0x31323334, "leaf0_ecx");

		run_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
		ASSERT_EQ(ecx & (1U << 31), 0, "leaf1_hypervisor_cleared");
		ASSERT_EQ(ecx & 1U, 1U, "leaf1_bit0_set");
	}

	if (test__start_subtest("multi_prog")) {
		link2 = bpf_program__attach_cgroup(skel->progs.cpuid_prog2,
						   cgroup_fd);
		if (!ASSERT_OK_PTR(link2, "attach_prog2"))
			goto cleanup_progs;

		run_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
		ASSERT_EQ(ebx, 0x46504265, "multi_leaf0_ebx");
		ASSERT_EQ(ecx, 0xdeadc0de, "multi_leaf0_ecx");

		bpf_link__destroy(link2);
		link2 = NULL;
	}

	if (test__start_subtest("cgroup_migration")) {
		int other_cg_fd;

		other_cg_fd = create_and_get_cgroup("/test-bpf-cpuid-other/");
		if (!ASSERT_OK_FD(other_cg_fd, "create_other_cgroup"))
			goto cleanup_progs;

		if (!ASSERT_OK(join_cgroup("/test-bpf-cpuid-other/"),
			       "join_other_cgroup")) {
			close(other_cg_fd);
			goto cleanup_progs;
		}

		run_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
		ASSERT_EQ(ebx, orig_ebx0, "unattached_cgroup_ebx");

		ASSERT_OK(join_cgroup(TEST_CGROUP), "rejoin_test_cgroup");
		run_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
		ASSERT_EQ(ebx, 0x46504265, "migrated_cgroup_ebx");

		close(other_cg_fd);
	}

	if (test__start_subtest("arch_prctl_interaction")) {
		/*
		 * Toggling ARCH_SET_CPUID off and back on must preserve
		 * CPUID faulting while a cgroup/cpuid BPF program is active.
		 */
		ASSERT_OK(syscall(SYS_arch_prctl, ARCH_SET_CPUID, 0),
			  "arch_prctl_disable");
		ASSERT_OK(syscall(SYS_arch_prctl, ARCH_SET_CPUID, 1),
			  "arch_prctl_enable");

		run_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
		ASSERT_EQ(ebx, 0x46504265, "after_arch_prctl_ebx");
	}

	if (test__start_subtest("deny_sigsegv")) {
		pid = fork();
		if (!ASSERT_GE(pid, 0, "fork"))
			goto cleanup_progs;

		if (pid == 0) {
			signal(SIGSEGV, SIG_DFL);
			run_cpuid(0xdeadbeef, 0, &eax, &ebx, &ecx, &edx);
			_exit(0);
		}

		ASSERT_EQ(waitpid(pid, &status, 0), pid, "waitpid");
		ASSERT_TRUE(WIFSIGNALED(status), "child_signaled");
		ASSERT_EQ(WTERMSIG(status), SIGSEGV, "child_sigsegv");
	}

	if (test__start_subtest("detach_restore")) {
		bpf_link__destroy(link1);
		link1 = NULL;

		run_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
		ASSERT_EQ(eax, orig_eax0, "restored_eax");
		ASSERT_EQ(ebx, orig_ebx0, "restored_ebx");
		ASSERT_EQ(ecx, orig_ecx0, "restored_ecx");
		ASSERT_EQ(edx, orig_edx0, "restored_edx");
	}

cleanup_progs:
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
	cpuid_cgroup__destroy(skel);
cleanup_cgroup:
	close(cgroup_fd);
	cleanup_cgroup_environment();
}
#else
void test_cgroup_cpuid(void)
{
	test__skip();
}
#endif
