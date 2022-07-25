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

#define TEST_HCALL(hc) { .nr = hc, .name = #hc }
#define UCALL_PR_HCALL 0xdeadc0de
#define PR_HCALL(hc) ucall(UCALL_PR_HCALL, 1, hc)

static void guest_main(void)
{
	asm("int $3");
	GUEST_DONE();
}

#define VCPU_ID 0

static void enter_guest(struct kvm_vm *vm)
{
	struct kvm_run *run;
	int i;

	run = vcpu_state(vm, VCPU_ID);

	for (i = 0; i < 1000000; i++) {
		int r = __vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(!r, "vcpu_run failed: %d\n", r);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "unexpected exit reason: %u (%s)",
			    run->exit_reason, exit_reason_str(run->exit_reason));
		syscall(999);
	}

}

int main(void)
{
	struct kvm_vm *vm;

	vm = vm_create_default(VCPU_ID, 0, guest_main);

	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vm, VCPU_ID);

	enter_guest(vm);
	kvm_vm_free(vm);
}
