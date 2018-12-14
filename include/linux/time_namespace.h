/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMENS_H
#define _LINUX_TIMENS_H


#include <linux/sched.h>
#include <linux/kref.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/err.h>
#include <linux/timens_offsets.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

struct time_namespace {
	struct kref kref;
	struct user_namespace *user_ns;
	struct ucounts *ucounts;
	struct ns_common ns;
	struct timens_offsets *offsets;
	bool   initialized;
} __randomize_layout;
extern struct time_namespace init_time_ns;

#ifdef CONFIG_TIME_NS
static inline struct time_namespace *get_time_ns(struct time_namespace *ns)
{
	kref_get(&ns->kref);
	return ns;
}

extern struct time_namespace *copy_time_ns(unsigned long flags,
	struct user_namespace *user_ns, struct time_namespace *old_ns);
extern void free_time_ns(struct kref *kref);
extern int timens_on_fork(struct nsproxy *nsproxy, struct task_struct *tsk);

static inline void put_time_ns(struct time_namespace *ns)
{
	kref_put(&ns->kref, free_time_ns);
}


extern void proc_timens_show_offsets(struct task_struct *p, struct seq_file *m);

struct proc_timens_offset {
	int clockid;
	struct timespec64 val;
};

extern int proc_timens_set_offset(struct task_struct *p,
				struct proc_timens_offset *offsets, int n);

extern void timens_clock_to_host(int clockid, struct timespec64 *val);
extern void timens_clock_from_host(int clockid, struct timespec64 *val);

#else
static inline struct time_namespace *get_time_ns(struct time_namespace *ns)
{
	return NULL;
}

static inline void put_time_ns(struct time_namespace *ns)
{
}

static inline struct time_namespace *copy_time_ns(unsigned long flags,
	struct user_namespace *user_ns, struct time_namespace *old_ns)
{
	if (flags & CLONE_NEWTIME)
		return ERR_PTR(-EINVAL);

	return old_ns;
}

static inline int timens_on_fork(struct nsproxy *nsproxy, struct task_struct *tsk)
{
	return 0;
}

static inline void timens_clock_to_host(int clockid, struct timespec64 *val)
{
}

static inline void timens_clock_from_host(int clockid, struct timespec64 *val)
{
}

#endif

#endif /* _LINUX_TIMENS_H */
