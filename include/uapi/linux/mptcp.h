/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_MPTCP_H
#define _UAPI_MPTCP_H

#ifndef __KERNEL__
#include <netinet/in.h>		/* for sockaddr_in and sockaddr_in6	*/
#include <sys/socket.h>		/* for struct sockaddr			*/
#endif

#include <linux/const.h>
#include <linux/types.h>
#include <linux/in.h>		/* for sockaddr_in			*/
#include <linux/in6.h>		/* for sockaddr_in6			*/
#include <linux/socket.h>	/* for sockaddr_storage and sa_family	*/

#define MPTCP_SUBFLOW_FLAG_MCAP_REM		_BITUL(0)
#define MPTCP_SUBFLOW_FLAG_MCAP_LOC		_BITUL(1)
#define MPTCP_SUBFLOW_FLAG_JOIN_REM		_BITUL(2)
#define MPTCP_SUBFLOW_FLAG_JOIN_LOC		_BITUL(3)
#define MPTCP_SUBFLOW_FLAG_BKUP_REM		_BITUL(4)
#define MPTCP_SUBFLOW_FLAG_BKUP_LOC		_BITUL(5)
#define MPTCP_SUBFLOW_FLAG_FULLY_ESTABLISHED	_BITUL(6)
#define MPTCP_SUBFLOW_FLAG_CONNECTED		_BITUL(7)
#define MPTCP_SUBFLOW_FLAG_MAPVALID		_BITUL(8)

#define MPTCP_PM_CMD_GRP_NAME	"mptcp_pm_cmds"
#define MPTCP_PM_EV_GRP_NAME	"mptcp_pm_events"

#include <linux/mptcp_pm.h>

#define MPTCP_INFO_FLAG_FALLBACK		_BITUL(0)
#define MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED	_BITUL(1)

#define MPTCP_PM_EV_FLAG_DENY_JOIN_ID0		_BITUL(0)
#define MPTCP_PM_EV_FLAG_SERVER_SIDE		_BITUL(1)

#define MPTCP_PM_ADDR_FLAG_SIGNAL		_BITUL(0)
#define MPTCP_PM_ADDR_FLAG_SUBFLOW		_BITUL(1)
#define MPTCP_PM_ADDR_FLAG_BACKUP		_BITUL(2)
#define MPTCP_PM_ADDR_FLAG_FULLMESH		_BITUL(3)
#define MPTCP_PM_ADDR_FLAG_IMPLICIT		_BITUL(4)
#define MPTCP_PM_ADDR_FLAG_LAMINAR		_BITUL(5)
#define MPTCP_PM_ADDR_FLAGS_MASK		GENMASK(5, 0)

struct mptcp_info {
	__u8	mptcpi_subflows;
	#define mptcpi_extra_subflows mptcpi_subflows
	__u8	mptcpi_add_addr_signal;
	__u8	mptcpi_add_addr_accepted;
	__u8	mptcpi_subflows_max;
	#define mptcpi_limit_extra_subflows mptcpi_subflows_max
	__u8	mptcpi_add_addr_signal_max;
	#define mptcpi_endp_signal_max mptcpi_add_addr_signal_max
	__u8	mptcpi_add_addr_accepted_max;
	#define mptcpi_limit_add_addr_accepted mptcpi_add_addr_accepted_max
	/* 16-bit hole that can no longer be filled */
	__u32	mptcpi_flags;
	__u32	mptcpi_token;
	__u64	mptcpi_write_seq;
	__u64	mptcpi_snd_una;
	__u64	mptcpi_rcv_nxt;
	__u8	mptcpi_local_addr_used;
	__u8	mptcpi_local_addr_max;
	#define mptcpi_endp_subflow_max mptcpi_local_addr_max
	__u8	mptcpi_csum_enabled;
	/* 8-bit hole that can no longer be filled */
	__u32	mptcpi_retransmits;
	__u64	mptcpi_bytes_retrans;
	__u64	mptcpi_bytes_sent;
	__u64	mptcpi_bytes_received;
	__u64	mptcpi_bytes_acked;
	__u8	mptcpi_subflows_total;
	__u8	mptcpi_endp_laminar_max;
	__u8	mptcpi_endp_fullmesh_max;
	__u8	reserved;
	__u32	mptcpi_last_data_sent;
	__u32	mptcpi_last_data_recv;
	__u32	mptcpi_last_ack_recv;
};

/* MPTCP Reset reason codes, rfc8684 */
#define MPTCP_RST_EUNSPEC	0
#define MPTCP_RST_EMPTCP	1
#define MPTCP_RST_ERESOURCE	2
#define MPTCP_RST_EPROHIBIT	3
#define MPTCP_RST_EWQ2BIG	4
#define MPTCP_RST_EBADPERF	5
#define MPTCP_RST_EMIDDLEBOX	6

struct mptcp_subflow_data {
	__u32		size_subflow_data;		/* size of this structure in userspace */
	__u32		num_subflows;			/* must be 0, set by kernel */
	__u32		size_kernel;			/* must be 0, set by kernel */
	__u32		size_user;			/* size of one element in data[] */
} __attribute__((aligned(8)));

struct mptcp_subflow_addrs {
	union {
		__kernel_sa_family_t sa_family;
		struct sockaddr sa_local;
		struct sockaddr_in sin_local;
		struct sockaddr_in6 sin6_local;
		struct __kernel_sockaddr_storage ss_local;
	};
	union {
		struct sockaddr sa_remote;
		struct sockaddr_in sin_remote;
		struct sockaddr_in6 sin6_remote;
		struct __kernel_sockaddr_storage ss_remote;
	};
};

struct mptcp_subflow_info {
	__u32				id;
	struct mptcp_subflow_addrs	addrs;
};

struct mptcp_full_info {
	__u32		size_tcpinfo_kernel;	/* must be 0, set by kernel */
	__u32		size_tcpinfo_user;
	__u32		size_sfinfo_kernel;	/* must be 0, set by kernel */
	__u32		size_sfinfo_user;
	__u32		num_subflows;		/* must be 0, set by kernel (real subflow count) */
	__u32		size_arrays_user;	/* max subflows that userspace is interested in;
						 * the buffers at subflow_info/tcp_info
						 * are respectively at least:
						 *  size_arrays * size_sfinfo_user
						 *  size_arrays * size_tcpinfo_user
						 * bytes wide
						 */
	__aligned_u64		subflow_info;
	__aligned_u64		tcp_info;
	struct mptcp_info	mptcp_info;
};

/* MPTCP socket options */
#define MPTCP_INFO		1
#define MPTCP_TCPINFO		2
#define MPTCP_SUBFLOW_ADDRS	3
#define MPTCP_FULL_INFO		4
#define MPTCP_REPAIR		10
#define MPTCP_REPAIR_KEYS	11
#define MPTCP_REPAIR_SEQ	12
#define MPTCP_REPAIR_SUBFLOW	13
#define MPTCP_REPAIR_QUEUE	14

/* Values for MPTCP_REPAIR */
#define MPTCP_REPAIR_OFF	0
#define MPTCP_REPAIR_ON		1
#define MPTCP_REPAIR_OFF_NO_WP	2

#define MPTCP_KEY_FLAG_CSUM_ENABLED	_BITUL(0)
#define MPTCP_KEY_FLAG_64BIT_ACK	_BITUL(1)
#define MPTCP_KEY_FLAG_FALLBACK		_BITUL(2)

struct mptcp_repair_keys {
	__u64	local_key;
	__u64	remote_key;
	__u32	token;
	__u32	flags;
};

/* Flags for MPTCP_REPAIR_SEQ */
#define MPTCP_SEQ_FLAG_RCV_DATA_FIN		_BITUL(0)
#define MPTCP_SEQ_FLAG_SND_DATA_FIN_ENABLE	_BITUL(1)
#define MPTCP_SEQ_FLAG_ALLOW_INFINITE_FALLBACK	_BITUL(2)

struct mptcp_repair_seq {
	__u64	write_seq;
	__u64	snd_nxt;
	__u64	snd_una;
	__u64	rcv_nxt;
	__u64	rcv_wnd_sent;
	__u64	rcv_data_fin_seq;
	__u32	mptcp_state;
	__u32	flags;
};

/* Flags for MPTCP_REPAIR_SUBFLOW */
#define MPTCP_SUBFLOW_REPAIR_FLAG_SACK_OK	_BITUL(0)
#define MPTCP_SUBFLOW_REPAIR_FLAG_TIMESTAMPS	_BITUL(1)
#define MPTCP_SUBFLOW_REPAIR_FLAG_BACKUP	_BITUL(2)
#define MPTCP_SUBFLOW_REPAIR_FLAG_JOIN		_BITUL(3)

struct mptcp_repair_subflow {
	struct mptcp_subflow_addrs addrs;
	__u32	subflow_id;
	__u32	snd_una;
	__u32	snd_nxt;
	__u32	rcv_nxt;
	__u32	snd_wnd;
	__u32	rcv_wnd;
	__u32	mss_clamp;
	__u32	ts_recent;
	__u32	ts_recent_stamp;
	__u32	tsoffset;
	__u16	flags;
	__u8	snd_wscale;
	__u8	rcv_wscale;
	__u8	local_id;
	__u8	remote_id;
	__u16	reserved;
	__u64	idsn;
	__u64	map_seq;
	__u32	map_subflow_seq;
	__u32	ssn_offset;
	__u32	rel_write_seq;
	__u32	reserved2;
};

enum {
	MPTCP_SEND_QUEUE = 0,
	MPTCP_RECV_QUEUE = 1,
	MPTCP_NO_QUEUE = 2,
	MPTCP_QUEUES_NR,
};

#endif /* _UAPI_MPTCP_H */
