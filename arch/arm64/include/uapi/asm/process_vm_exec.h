/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _ASM_UAPI_LINUX_PROCESS_VM_EXEC_H
#define _ASM_UAPI_LINUX_PROCESS_VM_EXEC_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

struct process_vm_exec_extctx {
	uint64_t tp_value;
	uint64_t fault_code;
};

#endif
