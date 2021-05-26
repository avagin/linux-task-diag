// SPDX-License-Identifier: GPL-2.0

#include <asm/syscall.h>
#include <asm/sigframe.h>
#include <asm/signal.h>
#include <asm/mmu_context.h>
#include <asm/sigcontext.h>

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/syscalls.h>
#include <linux/vmacache.h>
#include <linux/process_vm_exec.h>

static void swap_mm(struct mm_struct *prev_mm, struct mm_struct *target_mm)
{
	struct task_struct *tsk = current;
	struct mm_struct *active_mm;

	task_lock(tsk);
	/* Hold off tlb flush IPIs while switching mm's */
	local_irq_disable();

	sync_mm_rss(prev_mm);

	vmacache_flush(tsk);

	active_mm = tsk->active_mm;
	if (active_mm != target_mm) {
		mmgrab(target_mm);
		tsk->active_mm = target_mm;
	}
	tsk->mm = target_mm;
	switch_mm_irqs_off(active_mm, target_mm, tsk);
	local_irq_enable();
	task_unlock(tsk);
#ifdef finish_arch_post_lock_switch
	finish_arch_post_lock_switch();
#endif

	if (active_mm != target_mm)
		mmdrop(active_mm);
}

void restore_vm_exec_context(struct pt_regs *regs)
{
	struct process_vm_exec_context __user *uctx;
	struct mm_struct *prev_mm, *target_mm;

	uctx = current->exec_mm->ctx;
	current->exec_mm->ctx = NULL;

	target_mm = current->exec_mm->mm;
	current->exec_mm->mm = NULL;
	prev_mm = current->mm;

	swap_mm(prev_mm, target_mm);

	mmput(prev_mm);
	mmdrop(target_mm);

	swap_vm_exec_context(uctx);
}

SYSCALL_DEFINE6(process_vm_exec, pid_t, pid, struct process_vm_exec_context __user *, uctx,
		unsigned long, flags, siginfo_t __user *, uinfo,
		sigset_t __user *, user_mask, size_t, sizemask)
{
	struct mm_struct *prev_mm, *mm;
	struct task_struct *tsk;
	long ret = -ESRCH;

	sigset_t mask;

	if (flags)
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

	current_pt_regs()->ax = 0;
	ret = swap_vm_exec_context(uctx);
	if (ret < 0)
		goto err_mm_put;

	if (!current->exec_mm) {
		ret = -ENOMEM;
		current->exec_mm = kmalloc(sizeof(*current->exec_mm), GFP_KERNEL);
		if (current->exec_mm == NULL)
			goto err_mm_put;
	}
	current->exec_mm->ctx = uctx;
	current->exec_mm->mm = current->mm;
	current->exec_mm->flags = flags;
	current->exec_mm->sigmask = mask;
	current->exec_mm->siginfo = uinfo;
	prev_mm = current->mm;

	mmgrab(prev_mm);
	swap_mm(prev_mm, mm);

	ret = current_pt_regs()->ax;

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
