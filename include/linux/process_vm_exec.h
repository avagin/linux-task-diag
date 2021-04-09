/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PROCESS_VM_EXEC_H
#define _LINUX_PROCESS_VM_EXEC_H

struct exec_mm {
	struct sigcontext *ctx;
	struct mm_struct *mm;
	unsigned long flags;
	sigset_t sigmask;
	siginfo_t __user *siginfo;
};

void free_exec_mm_struct(struct task_struct *tsk);

#endif
