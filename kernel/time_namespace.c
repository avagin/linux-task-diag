// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Andrei Vagin <avagin@openvz.org>
 * Author: Dmitry Safonov <dima@arista.com>
 */

#include <linux/time_namespace.h>
#include <linux/user_namespace.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/proc_ns.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/mm.h>

static struct ucounts *inc_time_namespaces(struct user_namespace *ns)
{
	return inc_ucount(ns, current_euid(), UCOUNT_TIME_NAMESPACES);
}

static void dec_time_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_TIME_NAMESPACES);
}

static struct time_namespace *create_time_ns(void)
{
	struct time_namespace *time_ns;

	time_ns = kmalloc(sizeof(struct time_namespace), GFP_KERNEL);
	if (time_ns) {
		kref_init(&time_ns->kref);
		time_ns->initialized = false;
	}
	return time_ns;
}

/*
 * Clone a new ns copying @old_ns, setting refcount to 1
 * @old_ns: namespace to clone
 * Return the new ns or ERR_PTR.
 */
static struct time_namespace *clone_time_ns(struct user_namespace *user_ns,
					  struct time_namespace *old_ns)
{
	struct time_namespace *ns;
	struct ucounts *ucounts;
	struct page *page;
	int err;

	err = -ENOSPC;
	ucounts = inc_time_namespaces(user_ns);
	if (!ucounts)
		goto fail;

	err = -ENOMEM;
	ns = create_time_ns();
	if (!ns)
		goto fail_dec;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		goto fail_free;
	ns->offsets = page_address(page);
	BUILD_BUG_ON(sizeof(*ns->offsets) > PAGE_SIZE);

	err = ns_alloc_inum(&ns->ns);
	if (err)
		goto fail_page;

	ns->ucounts = ucounts;
	ns->ns.ops = &timens_operations;
	ns->user_ns = get_user_ns(user_ns);
	return ns;
fail_page:
	free_page((unsigned long)ns->offsets);
fail_free:
	kfree(ns);
fail_dec:
	dec_time_namespaces(ucounts);
fail:
	return ERR_PTR(err);
}

/*
 * Add a reference to old_ns, or clone it if @flags specify CLONE_NEWTIME.
 * In latter case, changes to the time of this process won't be seen by parent,
 * and vice versa.
 */
struct time_namespace *copy_time_ns(unsigned long flags,
	struct user_namespace *user_ns, struct time_namespace *old_ns)
{
	if (!(flags & CLONE_NEWTIME))
		return get_time_ns(old_ns);

	return clone_time_ns(user_ns, old_ns);
}

void free_time_ns(struct kref *kref)
{
	struct time_namespace *ns;

	ns = container_of(kref, struct time_namespace, kref);
	free_page((unsigned long)ns->offsets);
	dec_time_namespaces(ns->ucounts);
	put_user_ns(ns->user_ns);
	ns_free_inum(&ns->ns);
	kfree(ns);
}

static struct time_namespace *to_time_ns(struct ns_common *ns)
{
	return container_of(ns, struct time_namespace, ns);
}

static struct ns_common *timens_get(struct task_struct *task)
{
	struct time_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = nsproxy->time_ns;
		get_time_ns(ns);
	}
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static struct ns_common *timens_for_children_get(struct task_struct *task)
{
	struct time_namespace *ns = NULL;
	struct nsproxy *nsproxy;

	task_lock(task);
	nsproxy = task->nsproxy;
	if (nsproxy) {
		ns = nsproxy->time_ns_for_children;
		get_time_ns(ns);
	}
	task_unlock(task);

	return ns ? &ns->ns : NULL;
}

static void timens_put(struct ns_common *ns)
{
	put_time_ns(to_time_ns(ns));
}

static int timens_install(struct nsproxy *nsproxy, struct ns_common *new)
{
	struct time_namespace *ns = to_time_ns(new);

	if (!thread_group_empty(current))
		return -EINVAL;

	if (!ns_capable(ns->user_ns, CAP_SYS_ADMIN) ||
	    !ns_capable(current_user_ns(), CAP_SYS_ADMIN))
		return -EPERM;

	get_time_ns(ns);
	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns);
	put_time_ns(nsproxy->time_ns_for_children);
	nsproxy->time_ns = ns;
	nsproxy->time_ns_for_children = ns;
	ns->initialized = true;
	return 0;
}

int timens_on_fork(struct nsproxy *nsproxy, struct task_struct *tsk)
{
	struct ns_common *nsc = &nsproxy->time_ns_for_children->ns;
	struct time_namespace *ns = to_time_ns(nsc);

	if (nsproxy->time_ns == nsproxy->time_ns_for_children)
		return 0;

	get_time_ns(ns);
	put_time_ns(nsproxy->time_ns);
	nsproxy->time_ns = ns;
	ns->initialized = true;

	return 0;
}

static struct user_namespace *timens_owner(struct ns_common *ns)
{
	return to_time_ns(ns)->user_ns;
}

const struct proc_ns_operations timens_operations = {
	.name		= "time",
	.type		= CLONE_NEWTIME,
	.get		= timens_get,
	.put		= timens_put,
	.install	= timens_install,
	.owner		= timens_owner,
};

const struct proc_ns_operations timens_for_children_operations = {
	.name		= "time_for_children",
	.type		= CLONE_NEWTIME,
	.get		= timens_for_children_get,
	.put		= timens_put,
	.install	= timens_install,
	.owner		= timens_owner,
};

struct time_namespace init_time_ns = {
	.kref = KREF_INIT(3),
	.user_ns = &init_user_ns,
	.ns.inum = PROC_TIME_INIT_INO,
	.ns.ops = &timens_operations,
};

static int __init time_ns_init(void)
{
	return 0;
}
subsys_initcall(time_ns_init);
