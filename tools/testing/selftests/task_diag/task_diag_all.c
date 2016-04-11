#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include <linux/netlink.h>
#include <netlink/msg.h>

#include "task_diag.h"
#include "task_diag_comm.h"

#ifndef SOL_NETLINK
#define SOL_NETLINK	270
#endif

#ifndef NETLINK_SCM_PID
#define NETLINK_SCM_PID	11
#endif

static void usage(char *name)
{
	pr_err("Usage: %s command [options]", name);
	pr_err(
"Commands:\n"
"\tall         - dump all processes\n"
"\tAll         - dump all threads\n"
"\tthreads     - dump all thread for the specified process\n"
"\tchildren    - dump all thread for the specified process\n"
"\tone         - dump the specified process\n"
"Options:\n"
"\t-p|--pid    - PID of the required process\n"
"\t-m|--maps   - dump memory regions\n"
"\t-s|--smaps  - dump statistics for memory regions\n"
"\t-c|--cred   - dump credentials"
);
}
int main(int argc, char *argv[])
{
	int exit_status = 1, fd;
	struct task_diag_pid *req;
	char nl_req[4096];
	struct nlmsghdr *hdr = (void *)nl_req;
	int last_pid = 0;
	int opt, idx;
	int err, size = 0;
	static const char short_opts[] = "p:cmsl";
	static struct option long_opts[] = {
		{ "pid",	required_argument, 0, 'p' },
		{ "maps",	no_argument, 0, 'm' },
		{ "smaps",	no_argument, 0, 's' },
		{ "cred",	no_argument, 0, 'c' },
		{ "cmdline",	no_argument, 0, 'l' },
		{},
	};

	hdr->nlmsg_len = nlmsg_total_size(0);

	req = nlmsg_data(hdr);
	size += nla_total_size(sizeof(*req));

	hdr->nlmsg_len += size;


	req->show_flags = TASK_DIAG_SHOW_BASE;

	if (argc < 2) {
		pr_err("Usage: %s type pid scm_pid", argv[0]);
		return 1;
	}

	req->pid = 0; /* dump all tasks by default */

	switch (argv[1][0]) {
	case 'c':
		req->dump_strategy = TASK_DIAG_DUMP_CHILDREN;
		break;
	case 't':
		req->dump_strategy = TASK_DIAG_DUMP_THREAD;
		break;
	case 'o':
		req->dump_strategy = TASK_DIAG_DUMP_ONE;
		break;
	case 'a':
		req->dump_strategy = TASK_DIAG_DUMP_ALL;
		req->pid = 0;
		break;
	case 'A':
		req->dump_strategy = TASK_DIAG_DUMP_ALL_THREAD;
		req->pid = 0;
		break;
	default:
		usage(argv[0]);
		return 1;
	}

	while (1) {
		idx = -1;
		opt = getopt_long(argc, argv, short_opts, long_opts, &idx);
		if (opt == -1)
			break;
		switch (opt) {
		case 'p':
			req->pid = atoi(optarg);
			break;
		case 'c':
			req->show_flags |= TASK_DIAG_SHOW_CRED;
			break;
		case 'm':
			req->show_flags |= TASK_DIAG_SHOW_VMA;
			break;
		case 's':
			req->show_flags |= TASK_DIAG_SHOW_VMA_STAT | TASK_DIAG_SHOW_VMA;
			break;
		case 'l':
			req->show_flags |= TASK_DIAG_SHOW_CMDLINE;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	fd = open("/proc/task-diag", O_RDWR);
	if (fd < 0)
		return -1;

	if (write(fd, hdr, hdr->nlmsg_len) != hdr->nlmsg_len)
		return -1;

	while (1) {
		char buf[163840];
		size = read(fd, buf, sizeof(buf));

		if (size < 0)
			goto err;

		if (size == 0)
			break;

		err = nlmsg_receive(buf, size, &show_task, &last_pid);
		if (err < 0)
			goto err;

		if (err == 0)
			break;
	}

	exit_status = 0;
err:
	return exit_status;
}
