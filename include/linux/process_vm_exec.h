/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PROCESS_VM_EXEC_H
#define _LINUX_PROCESS_VM_EXEC_H

#include <uapi/asm/process_vm_exec.h>
#include <uapi/linux/process_vm_exec.h>

struct exec_mm {
	struct process_vm_exec_context *ctx;
	struct mm_struct *mm;
	unsigned long flags;
	sigset_t sigmask;
	siginfo_t __user *siginfo;
};

void free_exec_mm_struct(struct task_struct *tsk);

extern long swap_vm_exec_context(struct process_vm_exec_context __user *uctx);

#endif
