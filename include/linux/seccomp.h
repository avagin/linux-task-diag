/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SECCOMP_H
#define _LINUX_SECCOMP_H

#include <uapi/linux/seccomp.h>
#include <linux/seccomp_types.h>

#define SECCOMP_FILTER_FLAG_MASK	(SECCOMP_FILTER_FLAG_TSYNC | \
					 SECCOMP_FILTER_FLAG_LOG | \
					 SECCOMP_FILTER_FLAG_SPEC_ALLOW | \
					 SECCOMP_FILTER_FLAG_NEW_LISTENER | \
					 SECCOMP_FILTER_FLAG_TSYNC_ESRCH | \
					 SECCOMP_FILTER_FLAG_WAIT_KILLABLE_RECV | \
					 SECCOMP_FILTER_FLAG_CPUID)

/* sizeof() the first published struct seccomp_notif_addfd */
#define SECCOMP_NOTIFY_ADDFD_SIZE_VER0 24
#define SECCOMP_NOTIFY_ADDFD_SIZE_LATEST SECCOMP_NOTIFY_ADDFD_SIZE_VER0

#ifdef CONFIG_SECCOMP

#include <linux/thread_info.h>
#include <linux/atomic.h>
#include <asm/seccomp.h>

extern bool __seccomp_permit_syscall(void);

#ifdef CONFIG_HAVE_ARCH_SECCOMP_FILTER
static __always_inline bool seccomp_permit_syscall(void)
{
	if (unlikely(test_syscall_work(SECCOMP)))
		return  __seccomp_permit_syscall();
	return true;
}
#else
extern void secure_computing_strict(int this_syscall);
#endif

extern long prctl_get_seccomp(void);
extern long prctl_set_seccomp(unsigned long, void __user *);

static inline int seccomp_mode(struct seccomp *s)
{
	return s->mode;
}

#else /* CONFIG_SECCOMP */

#include <linux/errno.h>

struct seccomp_data;

#ifdef CONFIG_HAVE_ARCH_SECCOMP_FILTER
static inline bool seccomp_permit_syscall(void) { return true; }
#else
static inline void secure_computing_strict(int this_syscall) { return; }
#endif
static inline bool __seccomp_permit_syscall(void) { return true; }

static inline long prctl_get_seccomp(void)
{
	return -EINVAL;
}

static inline long prctl_set_seccomp(unsigned long arg2, char __user *arg3)
{
	return -EINVAL;
}

static inline int seccomp_mode(struct seccomp *s)
{
	return SECCOMP_MODE_DISABLED;
}
#endif /* CONFIG_SECCOMP */

#ifdef CONFIG_SECCOMP_FILTER
extern void seccomp_filter_release(struct task_struct *tsk);
extern void get_seccomp_filter(struct task_struct *tsk);
#else  /* CONFIG_SECCOMP_FILTER */
static inline void seccomp_filter_release(struct task_struct *tsk)
{
	return;
}
static inline void get_seccomp_filter(struct task_struct *tsk)
{
	return;
}
#endif /* CONFIG_SECCOMP_FILTER */

/**
 * enum seccomp_cpuid_result - how to complete a trapped CPUID instruction
 * @SECCOMP_CPUID_ALLOW: execute CPUID natively
 * @SECCOMP_CPUID_EMULATE: complete CPUID with the returned register values
 * @SECCOMP_CPUID_DENY: deny CPUID; the caller delivers SIGSEGV
 * @SECCOMP_CPUID_RETRY: a signal is pending (SIGSYS for TRAP and KILL
 *	actions, or a signal interrupted a user notification); leave the
 *	registers unchanged so CPUID is re-executed if the task survives
 */
enum seccomp_cpuid_result {
	SECCOMP_CPUID_ALLOW,
	SECCOMP_CPUID_EMULATE,
	SECCOMP_CPUID_DENY,
	SECCOMP_CPUID_RETRY,
};

#if defined(CONFIG_SECCOMP_FILTER) && defined(CONFIG_HAVE_ARCH_SECCOMP_CPUID)
extern enum seccomp_cpuid_result seccomp_filter_cpuid(u32 arch,
						      unsigned long ip,
						      u32 eax, u32 ecx,
						      u32 regs[4]);

/* Provided by architectures that select HAVE_ARCH_SECCOMP_CPUID */
extern bool arch_seccomp_cpuid_supported(void);
extern void arch_seccomp_cpuid_enable(struct task_struct *task);
extern void arch_seccomp_cpuid_sync(struct task_struct *task);
#else
static inline enum seccomp_cpuid_result
seccomp_filter_cpuid(u32 arch, unsigned long ip, u32 eax, u32 ecx, u32 regs[4])
{
	return SECCOMP_CPUID_DENY;
}

static inline bool arch_seccomp_cpuid_supported(void)
{
	return false;
}

static inline void arch_seccomp_cpuid_enable(struct task_struct *task) { }
static inline void arch_seccomp_cpuid_sync(struct task_struct *task) { }
#endif

#if defined(CONFIG_SECCOMP_FILTER) && defined(CONFIG_CHECKPOINT_RESTORE)
extern long seccomp_get_filter(struct task_struct *task,
			       unsigned long filter_off, void __user *data);
extern long seccomp_get_metadata(struct task_struct *task,
				 unsigned long filter_off, void __user *data);
#else
static inline long seccomp_get_filter(struct task_struct *task,
				      unsigned long n, void __user *data)
{
	return -EINVAL;
}
static inline long seccomp_get_metadata(struct task_struct *task,
					unsigned long filter_off,
					void __user *data)
{
	return -EINVAL;
}
#endif /* CONFIG_SECCOMP_FILTER && CONFIG_CHECKPOINT_RESTORE */

#ifdef CONFIG_SECCOMP_CACHE_DEBUG
struct seq_file;
struct pid_namespace;
struct pid;

int proc_pid_seccomp_cache(struct seq_file *m, struct pid_namespace *ns,
			   struct pid *pid, struct task_struct *task);
#endif
#endif /* _LINUX_SECCOMP_H */
