// SPDX-License-Identifier: GPL-2.0

#include <asm/signal.h>
#include <asm/sigcontext.h>

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/mmu_context.h>
#include <linux/process_vm_exec.h>

void swap_mm(struct mm_struct *prev_mm, struct mm_struct *target_mm)
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

