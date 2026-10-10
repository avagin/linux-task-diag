// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("cgroup/cpuid")
int cpuid_prog1(struct bpf_cpuid *ctx)
{
	if (ctx->eax_in == 0xdeadbeef)
		return 0;

	if (ctx->eax_in == 0 && ctx->ecx_in == 0) {
		ctx->ebx = 0x46504265; /* "eBPF" */
		ctx->edx = 0x54534554; /* "TEST" */
		ctx->ecx = 0x31323334; /* "4321" */
	} else if (ctx->eax_in == 1) {
		/* Clear hypervisor bit (bit 31) and force bit 0 of ecx */
		ctx->ecx &= ~(1U << 31);
		ctx->ecx |= 1U;
	}

	return 1;
}

SEC("cgroup/cpuid")
int cpuid_prog2(struct bpf_cpuid *ctx)
{
	if (ctx->eax_in == 0 && ctx->ecx_in == 0 && ctx->ebx == 0x46504265)
		ctx->ecx = 0xdeadc0de;

	return 1;
}

char _license[] SEC("license") = "GPL";
