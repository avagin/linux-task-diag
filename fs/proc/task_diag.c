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

	return task_state_array[__get_task_state(tsk)];
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

static int task_diag_fill(struct task_struct *tsk, struct sk_buff *skb,
			  struct task_diag_pid *req,
			  struct task_diag_cb *cb, struct pid_namespace *pidns,
			  struct user_namespace *userns)
{
	u64 show_flags = req->show_flags;
	struct nlmsghdr *nlh;
	struct task_diag_msg *msg;
	int err = 0, i = 0, n = 0;
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

	if (show_flags & TASK_DIAG_SHOW_BASE) {
		if (i >= n)
			err = fill_task_base(tsk, skb, pidns);
		if (err)
			goto err;
		i++;
	}

	nlmsg_end(skb, nlh);
	if (cb)
		cb->attr = 0;

	return 0;
err:
	if (err == -EMSGSIZE && (i > n)) {
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
	default:
		task = iter->task;
	}
	if (task)
		put_task_struct(task);
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

	case TASK_DIAG_DUMP_ALL:
		iter->tgid.tgid = iter->cb->pid;
		iter->tgid.task = NULL;
		iter->tgid = next_tgid(iter->ns, iter->tgid);
		return iter->tgid.task;
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

	case TASK_DIAG_DUMP_ALL:
		iter->tgid.tgid += 1;
		iter->tgid = next_tgid(iter->ns, iter->tgid);
		iter->cb->pid = iter->tgid.tgid;
		return iter->tgid.task;
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
