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

#define TASK_DIAG_FLAG_CONT 0x00000001

enum {
	TASK_DIAG_BASE	= 0,
	TASK_DIAG_CRED,
	TASK_DIAG_VMA,
	TASK_DIAG_VMA_STAT,
	TASK_DIAG_STAT,
	TASK_DIAG_STATM,
	TASK_DIAG_CMDLINE,

	__TASK_DIAG_ATTR_MAX
#define TASK_DIAG_ATTR_MAX (__TASK_DIAG_ATTR_MAX - 1)
};

#define TASK_DIAG_SHOW_BASE	(1ULL << TASK_DIAG_BASE)
#define TASK_DIAG_SHOW_CRED	(1ULL << TASK_DIAG_CRED)
#define TASK_DIAG_SHOW_VMA	(1ULL << TASK_DIAG_VMA)
#define TASK_DIAG_SHOW_VMA_STAT	(1ULL << TASK_DIAG_VMA_STAT)
#define TASK_DIAG_SHOW_STAT	(1ULL << TASK_DIAG_STAT)
#define TASK_DIAG_SHOW_STATM	(1ULL << TASK_DIAG_STATM)
#define TASK_DIAG_SHOW_CMDLINE	(1ULL << TASK_DIAG_CMDLINE)

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

#define TASK_DIAG_VMA_F_READ		(1ULL <<  0)
#define TASK_DIAG_VMA_F_WRITE		(1ULL <<  1)
#define TASK_DIAG_VMA_F_EXEC		(1ULL <<  2)
#define TASK_DIAG_VMA_F_SHARED		(1ULL <<  3)
#define TASK_DIAG_VMA_F_MAYREAD		(1ULL <<  4)
#define TASK_DIAG_VMA_F_MAYWRITE	(1ULL <<  5)
#define TASK_DIAG_VMA_F_MAYEXEC		(1ULL <<  6)
#define TASK_DIAG_VMA_F_MAYSHARE	(1ULL <<  7)
#define TASK_DIAG_VMA_F_GROWSDOWN	(1ULL <<  8)
#define TASK_DIAG_VMA_F_PFNMAP		(1ULL <<  9)
#define TASK_DIAG_VMA_F_DENYWRITE	(1ULL << 10)
#define TASK_DIAG_VMA_F_MPX		(1ULL << 11)
#define TASK_DIAG_VMA_F_LOCKED		(1ULL << 12)
#define TASK_DIAG_VMA_F_IO		(1ULL << 13)
#define TASK_DIAG_VMA_F_SEQ_READ	(1ULL << 14)
#define TASK_DIAG_VMA_F_RAND_READ	(1ULL << 15)
#define TASK_DIAG_VMA_F_DONTCOPY	(1ULL << 16)
#define TASK_DIAG_VMA_F_DONTEXPAND	(1ULL << 17)
#define TASK_DIAG_VMA_F_ACCOUNT		(1ULL << 18)
#define TASK_DIAG_VMA_F_NORESERVE	(1ULL << 19)
#define TASK_DIAG_VMA_F_HUGETLB		(1ULL << 20)
#define TASK_DIAG_VMA_F_ARCH_1		(1ULL << 21)
#define TASK_DIAG_VMA_F_DONTDUMP	(1ULL << 22)
#define TASK_DIAG_VMA_F_SOFTDIRTY	(1ULL << 23)
#define TASK_DIAG_VMA_F_MIXEDMAP	(1ULL << 24)
#define TASK_DIAG_VMA_F_HUGEPAGE	(1ULL << 25)
#define TASK_DIAG_VMA_F_NOHUGEPAGE	(1ULL << 26)
#define TASK_DIAG_VMA_F_MERGEABLE	(1ULL << 27)

struct task_diag_vma_stat {
	__u64 resident;
	__u64 shared_clean;
	__u64 shared_dirty;
	__u64 private_clean;
	__u64 private_dirty;
	__u64 referenced;
	__u64 anonymous;
	__u64 anonymous_thp;
	__u64 swap;
	__u64 pss;
} __attribute__((__aligned__(NLA_ALIGNTO)));

/* task_diag_vma must be NLA_ALIGN'ed */
struct task_diag_vma {
	__u64 start, end;
	__u64 vm_flags;
	__u64 pgoff;
	__u32 major;
	__u32 minor;
	__u64 inode;
	__u32 generation;
	__u16 vma_len;
	__u16 name_off;
	__u16 name_len;
	__u16 stat_off;
	__u16 stat_len;
} __attribute__((__aligned__(NLA_ALIGNTO)));

static inline char *task_diag_vma_name(struct task_diag_vma *vma)
{
	if (!vma->name_len)
		return NULL;

	return ((char *)vma) + vma->name_off;
}

static inline
struct task_diag_vma_stat *task_diag_vma_stat(struct task_diag_vma *vma)
{
	if (!vma->stat_len)
		return NULL;

	return ((void *)vma) + vma->stat_off;
}

#define task_diag_for_each_vma(vma, attr)			\
	for (vma = nla_data(attr);				\
		(void *) vma < nla_data(attr) + nla_len(attr);	\
		vma = (void *) vma + vma->vma_len)

struct task_diag_stat {
	__u64 minflt;
	__u64 cminflt;
	__u64 majflt;
	__u64 cmajflt;
	__u64 utime;
	__u64 stime;
	__u64 cutime;
	__u64 cstime;

	__u32 threads;
};

struct task_diag_statm {
	__u64 anon;
	__u64 file;
	__u64 shmem;
	__u64 total_vm;
	__u64 total_rss;
	__u64 hiwater_vm;
	__u64 hiwater_rss;
	__u64 text;
	__u64 lib;
	__u64 swap;
	__u64 ptes;
	__u64 locked_vm;
	__u64 pinned_vm;
	__u64 data_vm;
	__u64 stack_vm;
};

#define TASK_DIAG_DUMP_ALL		0
#define TASK_DIAG_DUMP_ONE		1
#define TASK_DIAG_DUMP_ALL_THREAD	2
#define TASK_DIAG_DUMP_CHILDREN		3
#define TASK_DIAG_DUMP_THREAD		4

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
