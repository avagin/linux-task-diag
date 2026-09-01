// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/tcp.h>
#include <linux/mptcp.h>

#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif
#ifndef SOL_MPTCP
#define SOL_MPTCP 284
#endif

#ifndef MPTCP_REPAIR
#define MPTCP_REPAIR		10
#define MPTCP_REPAIR_FLAGS	11
#define MPTCP_REPAIR_KEYS	12
#define MPTCP_REPAIR_SEQ	13
#define MPTCP_REPAIR_SUBFLOW	14
#define MPTCP_REPAIR_QUEUE	15

#define MPTCP_REPAIR_OFF	0
#define MPTCP_REPAIR_ON		1
#define MPTCP_REPAIR_OFF_NO_WP	2

struct mptcp_repair_keys {
	__u64	local_key;
	__u64	remote_key;
	__u32	token;
	__u32	flags;
};

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
#endif

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static int create_mptcp_pair(int *srv_fd, int *cli_fd)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = 0,
	};
	socklen_t addrlen = sizeof(addr);
	int listen_fd, client_fd, server_fd;

	listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
	if (listen_fd < 0)
		return -errno;

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(listen_fd);
		return -errno;
	}

	if (listen(listen_fd, 1) < 0) {
		close(listen_fd);
		return -errno;
	}

	if (getsockname(listen_fd, (struct sockaddr *)&addr, &addrlen) < 0) {
		close(listen_fd);
		return -errno;
	}

	client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
	if (client_fd < 0) {
		close(listen_fd);
		return -errno;
	}

	if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(listen_fd);
		close(client_fd);
		return -errno;
	}

	server_fd = accept(listen_fd, NULL, NULL);
	close(listen_fd);
	if (server_fd < 0) {
		close(client_fd);
		return -errno;
	}

	*srv_fd = server_fd;
	*cli_fd = client_fd;
	return 0;
}

static void test_mptcp_repair_basic(void)
{
	int srv_fd = -1, cli_fd = -1, ret, val;
	socklen_t optlen;
	struct mptcp_repair_keys keys;
	struct mptcp_repair_seq seq;
	struct mptcp_repair_subflow sf;
	char msg1[] = "Hello MPTCP Repair before checkpoint!";
	char msg2[] = "Hello MPTCP Repair after restore!";
	char buf[256];

	printf("Testing basic MPTCP repair mode dump & restore...\n");

	ret = create_mptcp_pair(&srv_fd, &cli_fd);
	if (ret < 0)
		die("Failed to create MPTCP socket pair: %s", strerror(-ret));

	/* Send initial data before repair */
	if (write(cli_fd, msg1, sizeof(msg1)) != sizeof(msg1))
		die("write msg1 failed: %s", strerror(errno));

	if (read(srv_fd, buf, sizeof(buf)) != sizeof(msg1))
		die("read msg1 failed: %s", strerror(errno));

	if (memcmp(msg1, buf, sizeof(msg1)) != 0)
		die("msg1 payload mismatch");

	printf("  Initial data exchange successful.\n");

	/* Enter repair mode on client socket */
	val = MPTCP_REPAIR_ON;
	if (setsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR, &val, sizeof(val)) < 0)
		die("setsockopt MPTCP_REPAIR ON failed: %s", strerror(errno));

	/* Verify repair state */
	val = 0;
	optlen = sizeof(val);
	if (getsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR, &val, &optlen) < 0)
		die("getsockopt MPTCP_REPAIR failed: %s", strerror(errno));
	if (val != MPTCP_REPAIR_ON)
		die("getsockopt MPTCP_REPAIR returned %d, expected %d", val, MPTCP_REPAIR_ON);

	/* Dump keys */
	optlen = sizeof(keys);
	if (getsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR_KEYS, &keys, &optlen) < 0)
		die("getsockopt MPTCP_REPAIR_KEYS failed: %s", strerror(errno));

	printf("  Dumped keys: local=0x%llx, remote=0x%llx, token=0x%x\n",
	       (unsigned long long)keys.local_key,
	       (unsigned long long)keys.remote_key,
	       keys.token);

	/* Dump sequences */
	optlen = sizeof(seq);
	if (getsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR_SEQ, &seq, &optlen) < 0)
		die("getsockopt MPTCP_REPAIR_SEQ failed: %s", strerror(errno));

	printf("  Dumped seq: write_seq=0x%llx, snd_nxt=0x%llx, rcv_nxt=0x%llx\n",
	       (unsigned long long)seq.write_seq,
	       (unsigned long long)seq.snd_nxt,
	       (unsigned long long)seq.rcv_nxt);

	/* Dump subflow */
	optlen = sizeof(sf);
	if (getsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR_SUBFLOW, &sf, &optlen) < 0)
		die("getsockopt MPTCP_REPAIR_SUBFLOW failed: %s", strerror(errno));

	printf("  Dumped subflow: snd_nxt=0x%x, rcv_nxt=0x%x, map_seq=0x%llx\n",
	       sf.snd_nxt, sf.rcv_nxt, (unsigned long long)sf.map_seq);

	/* Close client in repair mode (no RST sent to server) */
	close(cli_fd);

	/* Create a new MPTCP socket to restore the client state */
	cli_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
	if (cli_fd < 0)
		die("socket new client failed: %s", strerror(errno));

	/* Put new socket into repair mode */
	val = MPTCP_REPAIR_ON;
	if (setsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR, &val, sizeof(val)) < 0)
		die("setsockopt restore MPTCP_REPAIR ON failed: %s", strerror(errno));

	/* Restore keys */
	if (setsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR_KEYS, &keys, sizeof(keys)) < 0)
		die("setsockopt restore MPTCP_REPAIR_KEYS failed: %s", strerror(errno));

	/* Restore sequences */
	if (setsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR_SEQ, &seq, sizeof(seq)) < 0)
		die("setsockopt restore MPTCP_REPAIR_SEQ failed: %s", strerror(errno));

	/* Restore subflow */
	if (setsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR_SUBFLOW, &sf, sizeof(sf)) < 0)
		die("setsockopt restore MPTCP_REPAIR_SUBFLOW failed: %s", strerror(errno));

	/* Turn repair mode off to unpause connection */
	val = MPTCP_REPAIR_OFF;
	if (setsockopt(cli_fd, SOL_MPTCP, MPTCP_REPAIR, &val, sizeof(val)) < 0)
		die("setsockopt restore MPTCP_REPAIR OFF failed: %s", strerror(errno));

	printf("  Restored client socket and exited repair mode.\n");

	/* Test sending post-restore data */
	if (write(cli_fd, msg2, sizeof(msg2)) != sizeof(msg2))
		die("write msg2 after restore failed: %s", strerror(errno));

	memset(buf, 0, sizeof(buf));
	if (read(srv_fd, buf, sizeof(buf)) != sizeof(msg2))
		die("read msg2 after restore failed: %s", strerror(errno));

	if (memcmp(msg2, buf, sizeof(msg2)) != 0)
		die("msg2 payload mismatch after restore");

	printf("  Post-restore data verification passed!\n");

	close(cli_fd);
	close(srv_fd);
}

int main(int argc, char *argv[])
{
	test_mptcp_repair_basic();
	printf("All MPTCP repair mode tests passed successfully!\n");
	return 0;
}
