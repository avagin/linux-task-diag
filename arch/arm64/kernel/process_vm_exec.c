// SPDX-License-Identifier: GPL-2.0

#include <asm/syscall.h>
#include <asm/signal.h>
#include <asm/mmu_context.h>
#include <asm/sigcontext.h>

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/syscalls.h>
#include <linux/vmacache.h>
#include <linux/process_vm_exec.h>

int syscall_trace_enter(struct pt_regs *regs);
void syscall_trace_exit(struct pt_regs *regs);
extern void invoke_syscall(struct pt_regs *regs, unsigned int scno,
			   unsigned int sc_nr,
			   const syscall_fn_t syscall_table[]);

SYSCALL_DEFINE6(process_vm_exec, pid_t, pid, struct process_vm_exec_context __user *, uctx,
		unsigned long, flags, siginfo_t __user *, uinfo,
		sigset_t __user *, user_mask, size_t, sizemask)
{
	struct pt_regs *regs = current_pt_regs();
	struct mm_struct *prev_mm, *mm;
	struct task_struct *tsk;
	long ret = -ESRCH;

	sigset_t mask;

	if (flags & ~PROCESS_VM_EXEC_SYSCALL)
		return -EINVAL;

	if (sizemask != sizeof(sigset_t))
		return -EINVAL;
	if (copy_from_user(&mask, user_mask, sizeof(mask)))
		return -EFAULT;

	sigdelsetmask(&mask, sigmask(SIGKILL) | sigmask(SIGSTOP));
	signotset(&mask);

	tsk = find_get_task_by_vpid(pid);
	if (!tsk) {
		ret = -ESRCH;
		goto err;
	}
	mm = mm_access(tsk, PTRACE_MODE_ATTACH_REALCREDS);
	put_task_struct(tsk);
	if (!mm || IS_ERR(mm)) {
		ret = IS_ERR(mm) ? PTR_ERR(mm) : -ESRCH;
		goto err;
	}

	if (!current->exec_mm) {
		ret = -ENOMEM;
		current->exec_mm = kmalloc(sizeof(*current->exec_mm), GFP_KERNEL);
		if (current->exec_mm == NULL)
			goto err_mm_put;
		current->exec_mm->ctx = NULL;
	}

	regs->regs[0] = 0;
	if (flags & PROCESS_VM_EXEC_SYSCALL)
		syscall_trace_exit(regs);

	ret = swap_vm_exec_context(uctx);
	if (ret < 0)
		goto err_mm_put;

	current->exec_mm->ctx = uctx;
	current->exec_mm->mm = current->mm;
	current->exec_mm->flags = flags;
	current->exec_mm->sigmask = mask;
	current->exec_mm->siginfo = uinfo;
	prev_mm = current->mm;

	mmgrab(prev_mm);
	swap_mm(prev_mm, mm);

	if (flags & PROCESS_VM_EXEC_SYSCALL) {
		u64 orig_x0 = regs->orig_x0;
		int scno = regs->regs[8];

		regs->syscallno = scno;
		regs->orig_x0 = regs->regs[0];

		scno = syscall_trace_enter(regs);

		invoke_syscall(regs, scno, __NR_syscalls, sys_call_table);

		syscall_trace_exit(regs);

		if (current->exec_mm && current->exec_mm->ctx) {
			restore_vm_exec_context(regs);
			forget_syscall(regs);
			regs->syscallno = __NR_process_vm_exec;
			regs->orig_x0 = orig_x0;
			regs->regs[0] = orig_x0;
		}
		scno = syscall_trace_enter(regs);
		return 0;
	}

	ret = current_pt_regs()->regs[0];

	return ret;
err_mm_put:
	mmput(mm);
err:
	return ret;
}

void free_exec_mm_struct(struct task_struct *p)
{
	kfree(p->exec_mm);
	p->exec_mm = NULL;
}
