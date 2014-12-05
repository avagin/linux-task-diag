#ifndef __TASK_DIAG_COMM__
#define __TASK_DIAG_COMM__

#include <stdio.h>

#include "task_diag.h"

/*
 * Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere. It is recommended that commercial grade applications use
 * libnl or libnetlink and use the interfaces provided by the library
 */
#define GENLMSG_DATA(glh)	((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)	(NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)		((void *)((char *)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)	(len - NLA_HDRLEN)

#define pr_err(fmt, ...)				\
		fprintf(stderr, "%s:%d" fmt"\n", __func__, __LINE__, ##__VA_ARGS__)

#define pr_perror(fmt, ...)				\
		fprintf(stderr, fmt " : %m\n", ##__VA_ARGS__)

extern int quiet;
#define pr_info(fmt, arg...)			\
	do {					\
		if (!quiet)			\
			printf(fmt, ##arg);	\
	} while (0)				\

int nlmsg_receive(void *buf, int len, int (*cb)(struct nlmsghdr *, void *), void *args);
extern int show_task(struct nlmsghdr *hdr, void *arg);

#endif /* __TASK_DIAG_COMM__ */
