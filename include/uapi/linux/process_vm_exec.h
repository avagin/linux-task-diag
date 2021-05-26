/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_PROCESS_VM_EXEC_H
#define _UAPI_LINUX_PROCESS_VM_EXEC_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define PROCESS_VM_EXEC_GOOGLE_V1 0x20210524

#define PROCESS_VM_EXEC_SYSCALL 0x1UL

struct process_vm_exec_context {
	uint64_t version;
	/* extctx is a thread state that isn't present in sigcontext. */
	struct process_vm_exec_extctx extctx;
	struct sigcontext sigctx;
};

#endif
