#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/task_diag.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>

#include <net/netlink.h>

#include "internal.h"

struct task_diag_cb {
	struct sk_buff		*req;
	struct sk_buff		*resp;
	const struct nlmsghdr	*nlh;
	pid_t			pid;
	int			pos;
	int			attr;
	union { /* per-attribute */
		struct {
			unsigned long mark;
		} vma;
	};
};

/*
 * The task state array is a strange "bitmap" of
 * reasons to sleep. Thus "running" is zero, and
 * you can test for combinations of others with
 * simple bit tests.
 */
static const __u8 task_state_array[] = {
	TASK_DIAG_RUNNING,
	TASK_DIAG_INTERRUPTIBLE,
	TASK_DIAG_UNINTERRUPTIBLE,
	TASK_DIAG_STOPPED,
	TASK_DIAG_TRACE_STOP,
	TASK_DIAG_DEAD,
	TASK_DIAG_ZOMBIE,
	TASK_DIAG_PARKED,
	TASK_DIAG_IDLE,
};

static inline const __u8 get_task_state(struct task_struct *tsk)
{
	BUILD_BUG_ON(1 + ilog2(TASK_REPORT_MAX) != ARRAY_SIZE(task_state_array));

	return task_state_array[task_state_index(tsk)];
}

static int fill_task_base(struct task_struct *p,
			  struct sk_buff *skb, struct pid_namespace *ns)
{
	struct task_diag_base *base;
	struct nlattr *attr;
	char tcomm[sizeof(p->comm)];
	struct task_struct *tracer;

	attr = nla_reserve(skb, TASK_DIAG_BASE, sizeof(struct task_diag_base));
	if (!attr)
		return -EMSGSIZE;

	base = nla_data(attr);

	rcu_read_lock();
	base->ppid = pid_alive(p) ?
		task_tgid_nr_ns(rcu_dereference(p->real_parent), ns) : 0;

	base->tpid = 0;
	tracer = ptrace_parent(p);
	if (tracer)
		base->tpid = task_pid_nr_ns(tracer, ns);

	base->tgid = task_tgid_nr_ns(p, ns);
	base->pid  = task_pid_nr_ns(p, ns);
	base->sid  = task_session_nr_ns(p, ns);
	base->pgid = task_pgrp_nr_ns(p, ns);

	rcu_read_unlock();

	get_task_comm(tcomm, p);
	memset(base->comm, 0, TASK_DIAG_COMM_LEN);
	strncpy(base->comm, tcomm, TASK_DIAG_COMM_LEN);

	base->state = get_task_state(p);

	return 0;
}

static inline void caps2diag(struct task_diag_caps *diag, const kernel_cap_t *cap)
{
	int i;

	for (i = 0; i < _LINUX_CAPABILITY_U32S_3; i++)
		diag->cap[i] = cap->cap[i];
}

static int fill_creds(struct task_struct *p, struct sk_buff *skb,
					struct user_namespace *user_ns)
{
	struct task_diag_creds *diag_cred;
	const struct cred *cred;
	struct nlattr *attr;

	attr = nla_reserve(skb, TASK_DIAG_CRED, sizeof(struct task_diag_creds));
	if (!attr)
		return -EMSGSIZE;

	diag_cred = nla_data(attr);

	cred = get_task_cred(p);

	caps2diag(&diag_cred->cap_inheritable, &cred->cap_inheritable);
	caps2diag(&diag_cred->cap_permitted, &cred->cap_permitted);
	caps2diag(&diag_cred->cap_effective, &cred->cap_effective);
	caps2diag(&diag_cred->cap_bset, &cred->cap_bset);

	diag_cred->uid   = from_kuid_munged(user_ns, cred->uid);
	diag_cred->euid  = from_kuid_munged(user_ns, cred->euid);
	diag_cred->suid  = from_kuid_munged(user_ns, cred->suid);
	diag_cred->fsuid = from_kuid_munged(user_ns, cred->fsuid);
	diag_cred->gid   = from_kgid_munged(user_ns, cred->gid);
	diag_cred->egid  = from_kgid_munged(user_ns, cred->egid);
	diag_cred->sgid  = from_kgid_munged(user_ns, cred->sgid);
	diag_cred->fsgid = from_kgid_munged(user_ns, cred->fsgid);

	put_cred(cred);

	return 0;
}

static u64 get_vma_flags(struct vm_area_struct *vma)
{
	u64 flags = 0;

	static const u64 mnemonics[BITS_PER_LONG] = {
		/*
		 * In case if we meet a flag we don't know about.
		 */
		[0 ... (BITS_PER_LONG-1)] = 0,

		[ilog2(VM_READ)]	= TASK_DIAG_VMA_F_READ,
		[ilog2(VM_WRITE)]	= TASK_DIAG_VMA_F_WRITE,
		[ilog2(VM_EXEC)]	= TASK_DIAG_VMA_F_EXEC,
		[ilog2(VM_SHARED)]	= TASK_DIAG_VMA_F_SHARED,
		[ilog2(VM_MAYREAD)]	= TASK_DIAG_VMA_F_MAYREAD,
		[ilog2(VM_MAYWRITE)]	= TASK_DIAG_VMA_F_MAYWRITE,
		[ilog2(VM_MAYEXEC)]	= TASK_DIAG_VMA_F_MAYEXEC,
		[ilog2(VM_MAYSHARE)]	= TASK_DIAG_VMA_F_MAYSHARE,
		[ilog2(VM_GROWSDOWN)]	= TASK_DIAG_VMA_F_GROWSDOWN,
		[ilog2(VM_PFNMAP)]	= TASK_DIAG_VMA_F_PFNMAP,
		[ilog2(VM_DENYWRITE)]	= TASK_DIAG_VMA_F_DENYWRITE,
#ifdef CONFIG_X86_INTEL_MPX
		[ilog2(VM_MPX)]		= TASK_DIAG_VMA_F_MPX,
#endif
		[ilog2(VM_LOCKED)]	= TASK_DIAG_VMA_F_LOCKED,
		[ilog2(VM_IO)]		= TASK_DIAG_VMA_F_IO,
		[ilog2(VM_SEQ_READ)]	= TASK_DIAG_VMA_F_SEQ_READ,
		[ilog2(VM_RAND_READ)]	= TASK_DIAG_VMA_F_RAND_READ,
		[ilog2(VM_DONTCOPY)]	= TASK_DIAG_VMA_F_DONTCOPY,
		[ilog2(VM_DONTEXPAND)]	= TASK_DIAG_VMA_F_DONTEXPAND,
		[ilog2(VM_ACCOUNT)]	= TASK_DIAG_VMA_F_ACCOUNT,
		[ilog2(VM_NORESERVE)]	= TASK_DIAG_VMA_F_NORESERVE,
		[ilog2(VM_HUGETLB)]	= TASK_DIAG_VMA_F_HUGETLB,
		[ilog2(VM_ARCH_1)]	= TASK_DIAG_VMA_F_ARCH_1,
		[ilog2(VM_DONTDUMP)]	= TASK_DIAG_VMA_F_DONTDUMP,
#ifdef CONFIG_MEM_SOFT_DIRTY
		[ilog2(VM_SOFTDIRTY)]	= TASK_DIAG_VMA_F_SOFTDIRTY,
#endif
		[ilog2(VM_MIXEDMAP)]	= TASK_DIAG_VMA_F_MIXEDMAP,
		[ilog2(VM_HUGEPAGE)]	= TASK_DIAG_VMA_F_HUGEPAGE,
		[ilog2(VM_NOHUGEPAGE)]	= TASK_DIAG_VMA_F_NOHUGEPAGE,
		[ilog2(VM_MERGEABLE)]	= TASK_DIAG_VMA_F_MERGEABLE,
	};
	size_t i;

	for (i = 0; i < BITS_PER_LONG; i++) {
		if (vma->vm_flags & (1UL << i))
			flags |= mnemonics[i];
	}

	return flags;
}

/*
 * use a tmp variable and copy to input arg to deal with
 * alignment issues. diag_vma contains u64 elements which
 * means extended load operations can be used and those can
 * require 8-byte alignment (e.g., sparc)
 */
static void fill_diag_vma(struct vm_area_struct *vma,
			  struct task_diag_vma *diag_vma)
{
	struct task_diag_vma tmp;

	/* We don't show the stack guard page in /proc/maps */
	tmp.start = vma->vm_start;
	tmp.end = vma->vm_end;
	tmp.vm_flags = get_vma_flags(vma);

	if (vma->vm_file) {
		struct inode *inode = file_inode(vma->vm_file);
		dev_t dev;

		dev = inode->i_sb->s_dev;
		tmp.major = MAJOR(dev);
		tmp.minor = MINOR(dev);
		tmp.inode = inode->i_ino;
		tmp.generation = inode->i_generation;
		tmp.pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
	} else {
		tmp.major = 0;
		tmp.minor = 0;
		tmp.inode = 0;
		tmp.generation = 0;
		tmp.pgoff = 0;
	}

	memcpy(diag_vma, &tmp, sizeof(*diag_vma));
}

static const char *get_vma_name(struct vm_area_struct *vma, char *page)
{
	const char *name = NULL;

	if (vma->vm_file) {
		name = d_path(&vma->vm_file->f_path, page, PAGE_SIZE);
		goto out;
	}

	if (vma->vm_ops && vma->vm_ops->name) {
		name = vma->vm_ops->name(vma);
		if (name)
			goto out;
	}

	name = arch_vma_name(vma);

out:
	return name;
}

static void fill_diag_vma_stat(struct vm_area_struct *vma,
				struct task_diag_vma_stat *stat)
{
	struct task_diag_vma_stat tmp;
	struct mem_size_stats mss;
	struct mm_walk smaps_walk = {
		.pmd_entry = smaps_pte_range,
		.mm = vma->vm_mm,
		.private = &mss,
	};

	memset(&mss, 0, sizeof(mss));
	memset(&tmp, 0, sizeof(tmp));

	/* mmap_sem is held in m_start */
	walk_page_vma(vma, &smaps_walk);

	tmp.resident		= mss.resident;
	tmp.pss			= mss.pss;
	tmp.shared_clean	= mss.shared_clean;
	tmp.private_clean	= mss.private_clean;
	tmp.private_dirty	= mss.private_dirty;
	tmp.referenced		= mss.referenced;
	tmp.anonymous		= mss.anonymous;
	tmp.anonymous_thp	= mss.anonymous_thp;
	tmp.swap		= mss.swap;

	memcpy(stat, &tmp, sizeof(*stat));
}

static int fill_vma(struct task_struct *p, struct sk_buff *skb,
		    struct task_diag_cb *cb, bool *progress, u64 show_flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	struct nlattr *attr = NULL;
	struct task_diag_vma *diag_vma;
	unsigned long mark = 0;
	char *page;
	int i, rc = -EMSGSIZE, size;

	if (cb)
		mark = cb->vma.mark;

	mm = p->mm;
	if (!mm || !atomic_inc_not_zero(&mm->mm_users))
		return 0;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (!page) {
		mmput(mm);
		return -ENOMEM;
	}

	size = NLA_ALIGN(sizeof(struct task_diag_vma));
	if (show_flags & TASK_DIAG_SHOW_VMA_STAT)
		size += NLA_ALIGN(sizeof(struct task_diag_vma_stat));

	down_read(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next, i++) {
		unsigned char *b = skb_tail_pointer(skb);
		const char *name;
		void *pfile;


		if (mark >= vma->vm_start)
			continue;

		/* setup pointer for next map */
		if (attr == NULL) {
			attr = nla_reserve(skb, TASK_DIAG_VMA, size);
			if (!attr)
				goto err;

			diag_vma = nla_data(attr);
		} else {
			diag_vma = nla_reserve_nohdr(skb, size);

			if (diag_vma == NULL) {
				nlmsg_trim(skb, b);
				goto out;
			}
		}

		fill_diag_vma(vma, diag_vma);

		if (show_flags & TASK_DIAG_SHOW_VMA_STAT) {
			struct task_diag_vma_stat *stat;

			stat = (void *) diag_vma + NLA_ALIGN(sizeof(*diag_vma));

			fill_diag_vma_stat(vma, stat);
			diag_vma->stat_len = sizeof(struct task_diag_vma_stat);
			diag_vma->stat_off = (void *) stat - (void *)diag_vma;
		} else {
			diag_vma->stat_len = 0;
			diag_vma->stat_off = 0;
		}

		name = get_vma_name(vma, page);
		if (IS_ERR(name)) {
			nlmsg_trim(skb, b);
			rc = PTR_ERR(name);
			goto out;
		}

		if (name) {
			diag_vma->name_len = strlen(name) + 1;

			/* reserves NLA_ALIGN(len) */
			pfile = nla_reserve_nohdr(skb, diag_vma->name_len);
			if (pfile == NULL) {
				nlmsg_trim(skb, b);
				goto out;
			}
			diag_vma->name_off = pfile - (void *) diag_vma;
			memcpy(pfile, name, diag_vma->name_len);
		} else {
			diag_vma->name_len = 0;
			diag_vma->name_off = 0;
		}

		mark = vma->vm_start;

		diag_vma->vma_len = skb_tail_pointer(skb) - (unsigned char *) diag_vma;

		*progress = true;
	}

	rc = 0;
	mark = 0;
out:
	if (*progress)
		attr->nla_len = skb_tail_pointer(skb) - (unsigned char *) attr;

err:
	up_read(&mm->mmap_sem);
	mmput(mm);
	free_page((unsigned long) page);
	if (cb)
		cb->vma.mark = mark;

	return rc;
}

static int fill_task_stat(struct task_struct *task, struct sk_buff *skb, int whole)
{
	struct task_diag_stat *st;
	struct nlattr *attr;

	int priority, nice;
	int num_threads = 0;
	unsigned long cmin_flt = 0, cmaj_flt = 0;
	unsigned long  min_flt = 0,  maj_flt = 0;
	u64 cutime, cstime, utime, stime;
	u64 cgtime, gtime;
	unsigned long flags;

	attr = nla_reserve(skb, TASK_DIAG_STAT, sizeof(struct task_diag_stat));
	if (!attr)
		return -EMSGSIZE;

	st = nla_data(attr);

	cutime = cstime = utime = stime = 0;
	cgtime = gtime = 0;
	if (lock_task_sighand(task, &flags)) {
		struct signal_struct *sig = task->signal;

		num_threads = get_nr_threads(task);

		cmin_flt = sig->cmin_flt;
		cmaj_flt = sig->cmaj_flt;
		cutime = sig->cutime;
		cstime = sig->cstime;
		cgtime = sig->cgtime;

		/* add up live thread stats at the group level */
		if (whole) {
			struct task_struct *t = task;

			do {
				min_flt += t->min_flt;
				maj_flt += t->maj_flt;
				gtime += task_gtime(t);
			} while_each_thread(task, t);

			min_flt += sig->min_flt;
			maj_flt += sig->maj_flt;
			thread_group_cputime_adjusted(task, &utime, &stime);
			gtime += sig->gtime;
		}

		unlock_task_sighand(task, &flags);
	}

	if (!whole) {
		min_flt = task->min_flt;
		maj_flt = task->maj_flt;
		task_cputime_adjusted(task, &utime, &stime);
		gtime = task_gtime(task);
	}

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" Unix priority/nice value  */
	priority = task_prio(task);
	nice = task_nice(task);


	st->minflt	= min_flt;
	st->cminflt	= cmin_flt;
	st->majflt	= maj_flt;
	st->cmajflt	= cmaj_flt;
	st->utime	= nsec_to_clock_t(utime);
	st->stime	= nsec_to_clock_t(stime);
	st->cutime	= nsec_to_clock_t(cutime);
	st->cstime	= nsec_to_clock_t(cstime);

	st->threads	= num_threads;

	return 0;
}

static int task_diag_fill(struct task_struct *tsk, struct sk_buff *skb,
			  struct task_diag_pid *req,
			  struct task_diag_cb *cb, struct pid_namespace *pidns,
			  struct user_namespace *userns)
{
	u64 show_flags = req->show_flags;
	struct nlmsghdr *nlh;
	struct task_diag_msg *msg;
	int err = 0, i = 0, n = 0;
	bool progress = false;
	int flags = 0;

	if (cb) {
		n = cb->attr;
		flags |= NLM_F_MULTI;
	}

	nlh = nlmsg_put(skb, 0, cb->nlh->nlmsg_seq,
			TASK_DIAG_CMD_GET, sizeof(*msg), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	msg = nlmsg_data(nlh);
	msg->pid  = task_pid_nr_ns(tsk, pidns);
	msg->tgid = task_tgid_nr_ns(tsk, pidns);
	msg->flags |= TASK_DIAG_FLAG_CONT;

	if (show_flags & TASK_DIAG_SHOW_BASE) {
		if (i >= n)
			err = fill_task_base(tsk, skb, pidns);
		if (err)
			goto err;
		i++;
	}

	if (show_flags & TASK_DIAG_SHOW_CRED) {
		if (i >= n)
			err = fill_creds(tsk, skb, userns);
		if (err)
			goto err;
		i++;
	}

	if (show_flags & TASK_DIAG_SHOW_VMA) {
		bool dump_vma = true;

		/* if the request is to dump all threads of all processes
		 * only show VMAs for group leader.
		 */
		if ((req->dump_strategy == TASK_DIAG_DUMP_ALL_THREAD ||
		     req->dump_strategy == TASK_DIAG_DUMP_THREAD) &&
		    !thread_group_leader(tsk))
			dump_vma = false;

		if (dump_vma && i >= n)
			err = fill_vma(tsk, skb, cb, &progress, show_flags);
		if (err)
			goto err;
		i++;
	}

	if (show_flags & TASK_DIAG_SHOW_STAT) {
		int whole = 1;

		if (req->dump_strategy == TASK_DIAG_DUMP_ALL_THREAD ||
		    req->dump_strategy == TASK_DIAG_DUMP_THREAD)
			whole = 0;

		if (i >= n)
			err = fill_task_stat(tsk, skb, whole);
		if (err)
			goto err;
		i++;
	}

	msg->flags &= ~TASK_DIAG_FLAG_CONT;

	nlmsg_end(skb, nlh);
	if (cb)
		cb->attr = 0;

	return 0;
err:
	if (err == -EMSGSIZE && (i > n || progress)) {
		if (cb)
			cb->attr = i;
		nlmsg_end(skb, nlh);
	} else
		nlmsg_cancel(skb, nlh);

	return err;
}

struct task_iter {
	struct task_diag_pid	req;
	struct pid_namespace	*ns;
	struct task_struct	*parent;

	struct task_diag_cb	*cb;

	struct tgid_iter	tgid;
	unsigned int		pos;
	struct task_struct	*task;
};

static void iter_stop(struct task_iter *iter)
{
	struct task_struct *task;

	if (iter->parent)
		put_task_struct(iter->parent);

	switch (iter->req.dump_strategy) {
	case TASK_DIAG_DUMP_ALL:
		task = iter->tgid.task;
		break;
	case TASK_DIAG_DUMP_ALL_THREAD:
		/* release both tgid task and thread task */
		if (iter->task)
			put_task_struct(iter->task);
		task = iter->tgid.task;
		break;
	default:
		task = iter->task;
	}
	if (task)
		put_task_struct(task);
}

static struct task_struct *
task_diag_next_child(struct task_struct *parent,
			struct task_struct *prev, unsigned int pos)
{
	struct task_struct *task;

	read_lock(&tasklist_lock);
	task = task_next_child(parent, prev, pos);
	if (prev)
		put_task_struct(prev);
	if (task)
		get_task_struct(task);
	read_unlock(&tasklist_lock);

	return task;
}

static struct task_struct *iter_start(struct task_iter *iter)
{
	if (iter->req.pid > 0) {
		rcu_read_lock();
		iter->parent = find_task_by_pid_ns(iter->req.pid, iter->ns);
		if (iter->parent)
			get_task_struct(iter->parent);
		rcu_read_unlock();
	}

	switch (iter->req.dump_strategy) {
	case TASK_DIAG_DUMP_ONE:
		if (iter->parent == NULL)
			return ERR_PTR(-ESRCH);
		iter->pos = iter->cb->pos;
		if (iter->pos == 0) {
			iter->task = iter->parent;
			iter->parent = NULL;
		} else
			iter->task = NULL;
		return iter->task;

	case TASK_DIAG_DUMP_THREAD:
		if (iter->parent == NULL)
			return ERR_PTR(-ESRCH);

		iter->pos = iter->cb->pos;
		iter->task = task_first_tid(task_pid(iter->parent),
					    iter->cb->pid,iter->pos, iter->ns);
		return iter->task;

	case TASK_DIAG_DUMP_CHILDREN:
		if (iter->parent == NULL)
			return ERR_PTR(-ESRCH);

		iter->pos = iter->cb->pos;
		iter->task = task_diag_next_child(iter->parent, NULL, iter->pos);
		return iter->task;

	case TASK_DIAG_DUMP_ALL:
		iter->tgid.tgid = iter->cb->pid;
		iter->tgid.task = NULL;
		iter->tgid = next_tgid(iter->ns, iter->tgid);
		return iter->tgid.task;

	case TASK_DIAG_DUMP_ALL_THREAD:
		iter->pos = iter->cb->pos;
		iter->tgid.tgid = iter->cb->pid;
		iter->tgid.task = NULL;
		iter->tgid = next_tgid(iter->ns, iter->tgid);
		if (!iter->tgid.task)
			return NULL;

		iter->task = task_first_tid(task_pid(iter->tgid.task),
						0, iter->pos, iter->ns);
		if (!iter->task) {
			iter->pos = 0;
			iter->tgid.tgid += 1;
			iter->tgid = next_tgid(iter->ns, iter->tgid);
			iter->task = iter->tgid.task;
			if (iter->task)
				get_task_struct(iter->task);
		}
		return iter->task;
	}

	return ERR_PTR(-EINVAL);
}

static struct task_struct *iter_next(struct task_iter *iter)
{
	switch (iter->req.dump_strategy) {
	case TASK_DIAG_DUMP_ONE:
		iter->pos++;
		iter->cb->pos = iter->pos;
		if (iter->task)
			put_task_struct(iter->task);
		iter->task = NULL;
		return NULL;

	case TASK_DIAG_DUMP_THREAD:
		iter->pos++;
		iter->task = task_next_tid(iter->task);
		iter->cb->pos = iter->pos;
		if (iter->task)
			iter->cb->pid = task_pid_nr_ns(iter->task, iter->ns);
		else
			iter->cb->pid = -1;
		return iter->task;
	case TASK_DIAG_DUMP_CHILDREN:
		iter->pos++;
		iter->task = task_diag_next_child(iter->parent, iter->task, iter->pos);
		iter->cb->pos = iter->pos;
		return iter->task;

	case TASK_DIAG_DUMP_ALL:
		iter->tgid.tgid += 1;
		iter->tgid = next_tgid(iter->ns, iter->tgid);
		iter->cb->pid = iter->tgid.tgid;
		return iter->tgid.task;

	case TASK_DIAG_DUMP_ALL_THREAD:
		iter->pos++;
		iter->task = task_next_tid(iter->task);
		if (!iter->task) {
			iter->pos = 0;
			iter->tgid.tgid += 1;
			iter->tgid = next_tgid(iter->ns, iter->tgid);
			iter->task = iter->tgid.task;
			if (iter->task)
				get_task_struct(iter->task);
		}

		/* save current position */
		iter->cb->pid = iter->tgid.tgid;
		iter->cb->pos = iter->pos;

		return iter->task;
	}

	return NULL;
}

static int __taskdiag_dumpit(struct task_iter *iter,
			     struct task_diag_cb *cb, struct task_struct **start)
{
	struct user_namespace *userns = current_user_ns();
	struct task_struct *task = *start;
	int rc;

	for (; task; task = iter_next(iter)) {
		if (!ptrace_may_access(task, PTRACE_MODE_READ_FSCREDS))
			continue;

		rc = task_diag_fill(task, cb->resp, &iter->req,
				cb, iter->ns, userns);
		if (rc < 0) {
			if (rc != -EMSGSIZE)
				return rc;
			break;
		}
	}
	*start = task;

	return 0;
}

static int taskdiag_dumpit(struct task_diag_cb *cb,
				struct pid_namespace *pidns,
				struct msghdr *msg, size_t len)
{
	struct sk_buff *skb = cb->resp;
	struct task_struct *task;
	struct task_iter iter;
	struct nlattr *na;
	size_t copied;
	int err;

	if (nlmsg_len(cb->nlh) < sizeof(iter.req))
		return -EINVAL;

	na = nlmsg_data(cb->nlh);
	if (na->nla_type < 0)
		return -EINVAL;

	memcpy(&iter.req, na, sizeof(iter.req));

	iter.ns     = pidns;
	iter.cb     = cb;
	iter.parent = NULL;
	iter.pos    = 0;
	iter.task   = NULL;

	task = iter_start(&iter);
	if (IS_ERR(task))
		return PTR_ERR(task);

	copied = 0;
	while (1) {
		err = __taskdiag_dumpit(&iter, cb, &task);
		if (err < 0)
			goto err;
		if (skb->len == 0)
			break;

		err = skb_copy_datagram_msg(skb, 0, msg, skb->len);
		if (err < 0)
			goto err;

		copied += skb->len;

		skb_trim(skb, 0);
		if (skb_tailroom(skb) + copied > len)
			break;

		if (signal_pending(current))
			break;
	}

	iter_stop(&iter);
	return copied;
err:
	iter_stop(&iter);
	return err;
}

static ssize_t task_diag_write(struct file *f, const char __user *buf,
						size_t len, loff_t *off)
{
	struct task_diag_cb *cb = f->private_data;
	struct sk_buff *skb;
	struct msghdr msg;
	struct iovec iov;
	int err;

	if (cb->req)
		return -EBUSY;
	if (len < nlmsg_total_size(0))
		return -EINVAL;

	err = import_single_range(WRITE, (void __user *) buf, len,
						&iov, &msg.msg_iter);
	if (unlikely(err))
		return err;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	msg.msg_flags = 0;

	skb = nlmsg_new(len, GFP_KERNEL);
	if (skb == NULL)
		return -ENOMEM;

	if (memcpy_from_msg(skb_put(skb, len), &msg, len)) {
		kfree_skb(skb);
		return -EFAULT;
	}

	memset(cb, 0, sizeof(*cb));
	cb->req = skb;
	cb->nlh = nlmsg_hdr(skb);

	return len;
}

static ssize_t task_diag_read(struct file *file, char __user *ubuf,
						size_t len, loff_t *off)
{
	struct pid_namespace *ns = file_inode(file)->i_sb->s_fs_info;
	struct task_diag_cb *cb = file->private_data;
	struct iovec iov;
	struct msghdr msg;
	int size, err;

	if (cb->req == NULL)
		return 0;

	err = import_single_range(READ, ubuf, len, &iov, &msg.msg_iter);
	if (unlikely(err))
		goto err;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	if (!cb->resp) {
		size = min_t(size_t, len, 16384);
		cb->resp = alloc_skb(size, GFP_KERNEL);
		if (cb->resp == NULL) {
			err = -ENOMEM;
			goto err;
		}
		/* Trim skb to allocated size. */
		skb_reserve(cb->resp, skb_tailroom(cb->resp) - size);
	}

	err = taskdiag_dumpit(cb, ns, &msg, len);

err:
	skb_trim(cb->resp, 0);
	if (err <= 0) {
		kfree_skb(cb->req);
		cb->req = NULL;
	}

	return err;
}

static int task_diag_open (struct inode *inode, struct file *f)
{
	f->private_data = kzalloc(sizeof(struct task_diag_cb), GFP_KERNEL);
	if (f->private_data == NULL)
		return -ENOMEM;

	return 0;
}

static int task_diag_release(struct inode *inode, struct file *f)
{
	struct task_diag_cb *cb = f->private_data;

	kfree_skb(cb->req);
	kfree_skb(cb->resp);

	kfree(f->private_data);
	return 0;
}

static const struct file_operations task_diag_fops = {
	.owner		= THIS_MODULE,
	.open		= task_diag_open,
	.release	= task_diag_release,
	.write		= task_diag_write,
	.read		= task_diag_read,
};

static __init int task_diag_init(void)
{
	if (!proc_create("task-diag", S_IRUGO | S_IWUGO, NULL, &task_diag_fops))
		return -ENOMEM;

	return 0;
}

static __exit void task_diag_exit(void)
{
	remove_proc_entry("task-diag", NULL);
}

module_init(task_diag_init);
module_exit(task_diag_exit);
