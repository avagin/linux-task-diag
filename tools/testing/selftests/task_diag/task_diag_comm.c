#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <linux/netlink.h>
#include <netlink/cli/utils.h>

#include "task_diag.h"
#include "task_diag_comm.h"

int quiet;

#define PSS_SHIFT 12

int nlmsg_receive(void *buf, int len, int (*cb)(struct nlmsghdr *, void *), void *args)
{
	struct nlmsghdr *hdr;

	for (hdr = (struct nlmsghdr *)buf;
			NLMSG_OK(hdr, len); hdr = NLMSG_NEXT(hdr, len)) {

		if (hdr->nlmsg_type == NLMSG_DONE) {
			int *len = (int *)NLMSG_DATA(hdr);

			if (*len < 0) {
				pr_err("ERROR %d reported by netlink (%s)\n",
					*len, strerror(-*len));
				return *len;
			}

			return 0;
		}

		if (hdr->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(hdr);

			if (hdr->nlmsg_len - sizeof(*hdr) < sizeof(struct nlmsgerr)) {
				pr_err("ERROR truncated\n");
				return -1;
			}

			if (err->error == 0)
				return 0;

			return -1;
		}
		if (cb && cb(hdr, args))
			return -1;
	}

	return 1;
}

int show_task(struct nlmsghdr *hdr, void *arg)
{
	int msg_len;
	struct msgtemplate *msg;
	struct task_diag_msg *diag_msg;
	struct nlattr *na;
	int *last_pid = arg;
	int len;

	msg_len = NLMSG_PAYLOAD(hdr, 0);

	msg = (struct msgtemplate *)hdr;
	diag_msg = NLMSG_DATA(msg);

#if 1
	if (diag_msg->pid != *last_pid)
		pr_info("Start getting information about %d\n", diag_msg->pid);
	else
		pr_info("Continue getting information about %d\n", diag_msg->pid);
#endif
	*last_pid = diag_msg->pid;

	na = ((void *) diag_msg) + NLMSG_ALIGN(sizeof(*diag_msg));
	len = NLMSG_ALIGN(sizeof(*diag_msg));
	while (len < msg_len) {
		len += NLA_ALIGN(na->nla_len);
		switch (na->nla_type) {
		case TASK_DIAG_BASE:
		{
			struct task_diag_base *msg;

			/* For nested attributes, na follows */
			msg = NLA_DATA(na);
			pr_info("pid %5d tgid %5d ppid %5d sid %5d pgid %5d comm %s\n",
				msg->pid, msg->tgid, msg->ppid, msg->sid, msg->pgid, msg->comm);
		}
		break;

		case TASK_DIAG_CRED:
		{
			struct task_diag_creds *creds;

			creds = NLA_DATA(na);
			pr_info("uid: %d %d %d %d\n", creds->uid,
					creds->euid, creds->suid, creds->fsuid);
			pr_info("gid: %d %d %d %d\n", creds->uid,
					creds->euid, creds->suid, creds->fsuid);
			pr_info("CapInh: %08x%08x\n",
						creds->cap_inheritable.cap[1],
						creds->cap_inheritable.cap[0]);
			pr_info("CapPrm: %08x%08x\n",
						creds->cap_permitted.cap[1],
						creds->cap_permitted.cap[0]);
			pr_info("CapEff: %08x%08x\n",
						creds->cap_effective.cap[1],
						creds->cap_effective.cap[0]);
			pr_info("CapBnd: %08x%08x\n", creds->cap_bset.cap[1],
						creds->cap_bset.cap[0]);
		}
		break;

		case TASK_DIAG_CMDLINE:
		{
			char *cmdline = NLA_DATA(na);
			long i;

			for (i = 0; i < nla_len(na); i++)
				if (cmdline[i] == 0)
					cmdline[i] = ' ';
			cmdline[i - 1] = 0;
			pr_info("cmdline: %s\n", cmdline);
		}
		break;

		case TASK_DIAG_VMA:
		{
			struct task_diag_vma *vma_tmp, vma;

			task_diag_for_each_vma(vma_tmp, na) {
				char *name;
				struct task_diag_vma_stat *stat_tmp, stat;

				name = task_diag_vma_name(vma_tmp);
				if (name == NULL)
					name = "";

				memcpy(&vma, vma_tmp, sizeof(vma));
				pr_info("%016llx-%016llx %016llx %s\n",
					vma.start, vma.end, vma.vm_flags, name);

				stat_tmp = task_diag_vma_stat(vma_tmp);
				if (stat_tmp)
					memcpy(&stat, stat_tmp, sizeof(stat));
				else
					memset(&stat, 0, sizeof(stat));

				pr_info(
					   "Size:           %8llu kB\n"
					   "Rss:            %8llu kB\n"
					   "Pss:            %8llu kB\n"
					   "Shared_Clean:   %8llu kB\n"
					   "Shared_Dirty:   %8llu kB\n"
					   "Private_Clean:  %8llu kB\n"
					   "Private_Dirty:  %8llu kB\n"
					   "Referenced:     %8llu kB\n"
					   "Anonymous:      %8llu kB\n"
					   "AnonHugePages:  %8llu kB\n"
					   "Swap:           %8llu kB\n",
					   (vma.end - vma.start) >> 10,
					   stat.resident >> 10,
					   (stat.pss >> (10 + PSS_SHIFT)),
					   stat.shared_clean  >> 10,
					   stat.shared_dirty  >> 10,
					   stat.private_clean >> 10,
					   stat.private_dirty >> 10,
					   stat.referenced >> 10,
					   stat.anonymous >> 10,
					   stat.anonymous_thp >> 10,
					   stat.swap >> 10);
			}
		}
		break;
		default:
			pr_info("Unknown nla_type %d\n",
				na->nla_type);
		}
		na = ((void *) diag_msg) + len;
	}

	return 0;
}
