// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020, Google LLC.
 *
 * Tests for KVM paravirtual feature disablement
 */
#include <asm/kvm_para.h>
#include <asm/ptrace.h>
#include <linux/kvm_para.h>
#include <stdint.h>

#include <linux/unistd.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

struct pt_regs regs_dup = {
	.rax = __NR_dup,
	.rdi = -1,
};

struct pt_regs regs_nosys = {
	.rax = -1,
};

struct hcall_data {
	const char *name;
	struct pt_regs *regs;
	long ret;
};

#define TEST_HCALL(hc) { .nr = hc, .name = #hc }
#define UCALL_PR_HCALL 0xdeadc0de
#define PR_HCALL(hc) ucall(UCALL_PR_HCALL, 1, hc)

/*
 * KVM hypercalls to test. Expect -KVM_ENOSYS when called, as the corresponding
 * features have been cleared in KVM_CPUID_FEATURES.
 */
static struct hcall_data hcalls_to_test[] = {
	{.name = "dup",    .regs = &regs_dup,   .ret = -EBADF},
	{.name = "enosys", .regs = &regs_nosys, .ret = -ENOSYS},
};

static void test_hcall(struct hcall_data *hc)
{
	uint64_t r;

	PR_HCALL(hc);
	r = kvm_hypercall(KVM_HC_HOST_SYSCALL, (unsigned long)hc->regs, 0, 0, 0);
	GUEST_ASSERT(r == 0);
}

static void guest_main(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hcalls_to_test); i++)
		test_hcall(&hcalls_to_test[i]);

	GUEST_DONE();
}

static void pr_hcall(struct ucall *uc)
{
	struct hcall_data *hc = (struct hcall_data *)uc->args[0];

	pr_info("testing hcall: %s\n", hc->name);
}

static void handle_abort(struct ucall *uc)
{
	TEST_FAIL("%s at %s:%ld", (const char *)uc->args[0],
		  __FILE__, uc->args[1]);
}

#define VCPU_ID 0

static void enter_guest(struct kvm_vm *vm)
{
	struct kvm_run *run;
	struct ucall uc;
	int r, i;

	run = vcpu_state(vm, VCPU_ID);

	while (true) {
		r = _vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(!r, "vcpu_run failed: %d\n", r);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "unexpected exit reason: %u (%s)",
			    run->exit_reason, exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_PR_HCALL:
			pr_hcall(&uc);
			break;
		case UCALL_ABORT:
			handle_abort(&uc);
			return;
		case UCALL_DONE:
			goto out;
		}
	}

out:
	for (i = 0; i < ARRAY_SIZE(hcalls_to_test); i++) {
		struct hcall_data *hc = &hcalls_to_test[i];

		TEST_ASSERT(hc->ret == hc->regs->rax, "%s: ret %ld (expected %ld)",
				hc->name, hc->ret, hc->regs->rax);
	}
}

int main(void)
{
	struct kvm_enable_cap cap = {0};
	struct kvm_cpuid2 *best;
	struct kvm_vm *vm;

	if (!kvm_check_cap(KVM_CAP_ENFORCE_PV_FEATURE_CPUID)) {
		pr_info("will skip kvm paravirt restriction tests.\n");
		return 0;
	}

	vm = vm_create_default(VCPU_ID, 0, guest_main);

	cap.cap = KVM_CAP_ENFORCE_PV_FEATURE_CPUID;
	cap.args[0] = 1;
	vcpu_enable_cap(vm, VCPU_ID, &cap);

	best = kvm_get_supported_cpuid();
	vcpu_set_cpuid(vm, VCPU_ID, best);

	cap.cap = KVM_CAP_PV_HOST_SYSCALL;
	cap.args[0] = 1;
	vcpu_enable_cap(vm, VCPU_ID, &cap);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	enter_guest(vm);
	kvm_vm_free(vm);
}
