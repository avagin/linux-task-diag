#ifndef _LINUX_TASK_DIAG_H
#define _LINUX_TASK_DIAG_H

#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/capability.h>

#define TASK_DIAG_CMD_GET 0xd101U

struct task_diag_msg {
	__u32 pid;
	__u32 tgid;
	__u32 flags;
};

enum {
	TASK_DIAG_BASE	= 0,
	TASK_DIAG_CRED,

	__TASK_DIAG_ATTR_MAX
#define TASK_DIAG_ATTR_MAX (__TASK_DIAG_ATTR_MAX - 1)
};

#define TASK_DIAG_SHOW_BASE	(1ULL << TASK_DIAG_BASE)
#define TASK_DIAG_SHOW_CRED	(1ULL << TASK_DIAG_CRED)

enum {
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

#define TASK_DIAG_COMM_LEN 16

struct task_diag_base {
	__u32	tgid;
	__u32	pid;
	__u32	ppid;
	__u32	tpid;
	__u32	sid;
	__u32	pgid;
	__u8	state;
	char	comm[TASK_DIAG_COMM_LEN];
};

struct task_diag_caps {
	__u32 cap[_LINUX_CAPABILITY_U32S_3];
};

struct task_diag_creds {
	struct task_diag_caps cap_inheritable;
	struct task_diag_caps cap_permitted;
	struct task_diag_caps cap_effective;
	struct task_diag_caps cap_bset;

	__u32 uid;
	__u32 euid;
	__u32 suid;
	__u32 fsuid;
	__u32 gid;
	__u32 egid;
	__u32 sgid;
	__u32 fsgid;
};
#define TASK_DIAG_DUMP_ALL	0
#define TASK_DIAG_DUMP_ONE	1

struct task_diag_pid {
	__u64	show_flags;
	__u64	dump_strategy;

	__u32	pid;
};

enum {
	TASK_DIAG_CMD_ATTR_UNSPEC = 0,
	TASK_DIAG_CMD_ATTR_GET,
	__TASK_DIAG_CMD_ATTR_MAX,
};

#define TASK_DIAG_CMD_ATTR_MAX (__TASK_DIAG_CMD_ATTR_MAX - 1)

#endif /* _LINUX_TASK_DIAG_H */
