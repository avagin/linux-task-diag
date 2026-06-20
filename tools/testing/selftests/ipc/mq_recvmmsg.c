// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include "kselftest.h"
#include <mqueue.h>

#ifndef __NR_mq_recvmmsg
#if defined(__alpha__)
#define __NR_mq_recvmmsg 582
#else
#define __NR_mq_recvmmsg 472
#endif
#endif

#define MQ_PEEK 0x02
#define MQ_RECV 0x04

#define MQ_NAME_PREFIX "/mq_peek_test"
#define MQ_MAXMSG 16
#define MAX_MSG_SIZE 8192

struct msg_attrs {
	long msg_len;
	unsigned int *msg_prio;
	void *msg_ptr;
};

struct mmsg_attrs {
	struct iovec *msg_attrs_vec;
	long vlen;
	int *err;
};

struct batch_slot{
	struct msg_attrs desc;
	struct iovec iov;
	char buf[MAX_MSG_SIZE];
	unsigned int prio;
};

static long mq_recvmmsg(mqd_t mqdes, struct mmsg_attrs *attrs, unsigned int attrs_len,
					   unsigned int flags, unsigned int start_idx,
					   const struct timespec *timeout)
{
	return syscall(__NR_mq_recvmmsg, mqdes, attrs, attrs_len, flags,
				  start_idx, timeout);
}

static long queue_depth(mqd_t mqd)
{
	struct mq_attr attr;

	if (mq_getattr(mqd, &attr))
		return -1;

	return attr.mq_curmsgs;
}

static mqd_t open_queue(const char *suffix)
{
	static unsigned int seq;
	char name[128];
	struct mq_attr attr = {
		.mq_flags = 0,
		.mq_maxmsg = MQ_MAXMSG,
		.mq_msgsize = MAX_MSG_SIZE,
	};

	mqd_t mqd;

	snprintf(name, sizeof(name), "%s%s_%d_%u", MQ_NAME_PREFIX, suffix, getpid(),
			seq++);

	mq_unlink(name);

	mqd = mq_open(name, O_NONBLOCK | O_RDWR | O_CREAT | O_EXCL, 0600, &attr);
	if (mqd == (mqd_t)-1) {
		ksft_test_result_fail("mq_open(%s) failed: %m\n", name);
		ksft_exit_fail();
	}

	mq_unlink(name);
	return mqd;
}

static void build_slot(struct batch_slot *slot)
{
	memset(slot, 0, sizeof(*slot));
	slot->desc.msg_len = sizeof(slot->buf);
	slot->desc.msg_prio = &slot->prio;
	slot->desc.msg_ptr = slot->buf;
	slot->iov.iov_base = &slot->desc;
	slot->iov.iov_len = sizeof(slot->desc);
}

static void build_slots(struct batch_slot *slots, size_t nr)
{
	size_t i;

	for (i = 0; i < nr; i++)
		build_slot(&slots[i]);
}

static long batch_recv(mqd_t mqd, struct batch_slot *slots, size_t nr,
					  int *err, unsigned int flags, unsigned int start_idx,
					  const struct timespec *timeout)
{
	struct iovec vec[MQ_MAXMSG];
	struct mmsg_attrs attrs;
	size_t i;

	for (i = 0; i < nr; i++)
		vec[i] = slots[i].iov;

	attrs.msg_attrs_vec = vec;
	attrs.vlen = nr;
	attrs.err = err;

	return mq_recvmmsg(mqd, &attrs, sizeof(attrs), flags, start_idx,
					  timeout);
}


static void send_or_die(mqd_t mqd, unsigned int prio, const char *payload)
{
	if (mq_send(mqd, payload, strlen(payload), prio)) {
		ksft_test_result_fail("mq_send(%s, %u) failed: %m\n",
				      payload, prio);
		ksft_exit_fail();
	}
}

static bool check_slot(const struct batch_slot *slot, const char *payload,
					  unsigned int prio)
{
	size_t len = strlen(payload);

	if (slot->prio != prio)
		return false;
	if (memcmp(slot->buf, payload, len))
		return false;
	return true;
}

static void test_peek_batch_success_count(void)
{
	mqd_t mqd = open_queue("peek_count");
	struct batch_slot slots[3];
	long ret;
	int err;

	send_or_die(mqd, 1, "low");
	send_or_die(mqd, 9, "high");

	build_slots(slots, ARRAY_SIZE(slots));
	ret = batch_recv(mqd, slots, ARRAY_SIZE(slots), &err, MQ_PEEK, 0, NULL);

	if (ret != 2) {
		ksft_test_result_fail("peek count: ret=%ld expected=2\n", ret);
		goto out;
	}
	if (!check_slot(&slots[0], "high", 9) ||
	    !check_slot(&slots[1], "low", 1)) {
		ksft_test_result_fail("peek count: payload/prio mismatch\n");
		goto out;
	}
	if (queue_depth(mqd) != 2) {
		ksft_test_result_fail("peek count: queue depth changed to %ld\n",
				      queue_depth(mqd));
		goto out;
	}

	ksft_test_result_pass("peek batch returns number of successful messages\n");
out:
	mq_close(mqd);
}

static void test_receive_batch_success_count(void)
{
	static const struct timespec expired = { 0, 0 };
	mqd_t mqd = open_queue("recv_count");
	struct batch_slot slots[3];
	long ret;
	int err;

	send_or_die(mqd, 2, "two");
	send_or_die(mqd, 7, "seven");

	build_slots(slots, ARRAY_SIZE(slots));
	ret = batch_recv(mqd, slots, ARRAY_SIZE(slots), &err, MQ_RECV, 0, &expired);

	if (ret != 2) {
		ksft_test_result_fail("receive count: ret=%ld expected=2\n", ret);
		goto out;
	}
	if (!check_slot(&slots[0], "seven", 7) ||
	    !check_slot(&slots[1], "two", 2)) {
		ksft_test_result_fail("receive count: payload/prio mismatch\n");
		goto out;
	}
	if (queue_depth(mqd) != 0) {
		ksft_test_result_fail("receive count: queue depth=%ld expected=0\n",
				      queue_depth(mqd));
		goto out;
	}

	ksft_test_result_pass("receive batch returns success count and consumes messages\n");
out:
	mq_close(mqd);
}

static void test_peek_start_idx_uses_absolute_index(void)
{
	mqd_t mqd = open_queue("peek_offset");
	struct batch_slot slots[3];
	long ret;
	int err;

	send_or_die(mqd, 9, "high");
	send_or_die(mqd, 5, "mid");
	send_or_die(mqd, 1, "low");

	build_slots(slots, ARRAY_SIZE(slots));
	memset(slots[0].buf, 0x5a, sizeof(slots[0].buf));
	ret = batch_recv(mqd, slots, ARRAY_SIZE(slots), &err, MQ_PEEK, 1, NULL);

	if (ret != 2) {
		ksft_test_result_fail("peek start_idx: ret=%ld expected=2\n", ret);
		goto out;
	}
	if (!check_slot(&slots[1], "mid", 5) ||
	    !check_slot(&slots[2], "low", 1)) {
		ksft_test_result_fail("peek start_idx: wrong messages returned\n");
		goto out;
	}
	if (slots[0].buf[0] != 0x5a) {
		ksft_test_result_fail("peek start_idx: slot 0 was written by kernel unnecessarily\n");
		goto out;
	}

	ksft_test_result_pass("peek start_idx maps directly to queue index and offset handling seems correct\n");
out:
	mq_close(mqd);
}

static void test_receive_start_idx_not_receives_from_outside_range(void)
{
	mqd_t mqd = open_queue("recv_offset");
	struct batch_slot slots[3];
	long ret;
	int err;

	send_or_die(mqd, 8, "first");
	send_or_die(mqd, 3, "second");

	build_slots(slots, ARRAY_SIZE(slots));
	memset(slots[0].buf, 0x6b, sizeof(slots[0].buf));
	ret = batch_recv(mqd, slots, ARRAY_SIZE(slots), &err, MQ_RECV, 1, NULL);

	if (ret != 2) {
		ksft_test_result_fail("receive start_idx: ret=%ld expected=2\n",
					ret);
		goto out;
	}
	if (!check_slot(&slots[1], "first", 8) ||
		!check_slot(&slots[2], "second", 3)) {
		ksft_test_result_fail("receive start_idx: wrong receive order\n");
		goto out;
	}
	if (slots[0].buf[0] != 0x6b) {
		ksft_test_result_fail("receive start_idx: slot 0 was written by kernel unnecessarily\n");
		goto out;
	}

	ksft_test_result_pass("non-peek path ignores queue indexing and receives from head within range\n");
out:
	mq_close(mqd);
}

static void test_peek_partial_success_on_small_buffer(void)
{
	mqd_t mqd = open_queue("peek_small");
	struct batch_slot slots[2];
	long ret;
	int err;

	send_or_die(mqd, 9, "abc");
	send_or_die(mqd, 4, "1234");

	build_slots(slots, ARRAY_SIZE(slots));
	slots[1].desc.msg_len = 1;
	ret = batch_recv(mqd, slots, ARRAY_SIZE(slots), &err, MQ_PEEK, 0, NULL);

	if (ret != 1) {
		ksft_test_result_fail("peek partial small buffer: ret=%ld expected=1\n",
					ret);
		goto out;
	}
	if (!check_slot(&slots[0], "abc", 9)) {
		ksft_test_result_fail("peek partial small buffer: first slot mismatch\n");
		goto out;
	}
	if (queue_depth(mqd) != 2) {
		ksft_test_result_fail("peek partial small buffer: queue depth=%ld expected=2\n",
					queue_depth(mqd));
		goto out;
	}

	ksft_test_result_pass("peek batch reports completed so far before EMSGSIZE\n");
out:
	mq_close(mqd);
}

static void test_receive_partial_success_on_bad_desc_len(void)
{
	mqd_t mqd = open_queue("recv_desc");
	struct batch_slot slots[2];
	long ret;
	int err;

	send_or_die(mqd, 6, "driver");
	send_or_die(mqd, 2, "module");

	build_slots(slots, ARRAY_SIZE(slots));
	slots[1].iov.iov_len = sizeof(slots[1].desc) - 1;
	ret = batch_recv(mqd, slots, ARRAY_SIZE(slots), &err, MQ_RECV, 0, NULL);

	if (ret != 1) {
		ksft_test_result_fail("receive partial desc len: ret=%ld expected=1\n",
					ret);
		goto out;
	}
	if (!check_slot(&slots[0], "driver", 6)) {
		ksft_test_result_fail("receive partial desc len: first slot mismatch\n");
		goto out;
	}
	if (queue_depth(mqd) != 1) {
		ksft_test_result_fail("receive partial desc len: queue depth=%ld expected=1\n",
					queue_depth(mqd));
		goto out;
	}

	ksft_test_result_pass("receive batch reports completed so far before descriptor error\n");
out:
	mq_close(mqd);
}

static const struct {
	const char *name;
	void (*fn)(void);
} tests[] = {
	{ "peek batch success count", test_peek_batch_success_count },
	{ "receive batch success count", test_receive_batch_success_count },
	{ "peek absolute index with start_idx", test_peek_start_idx_uses_absolute_index },
	{ "receive start_idx obey offset boundary", test_receive_start_idx_not_receives_from_outside_range },
	{ "peek partial success before EMSGSIZE", test_peek_partial_success_on_small_buffer },
	{ "receive partial success before desc error", test_receive_partial_success_on_bad_desc_len },
};

int main(void)
{
	size_t i;
	struct batch_slot probe;
	long ret;
	int err;

	ksft_print_header();

	build_slot(&probe);
	ret = batch_recv((mqd_t)-1, &probe, 1, &err, MQ_PEEK, 0, NULL);
	if (ret == -1 && errno == ENOSYS)
		ksft_exit_skip("mq_recvmmsg syscall not available\n");

	ksft_set_plan(ARRAY_SIZE(tests));

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		ksft_print_msg("[%02zu] %s\n", i + 1, tests[i].name);
		tests[i].fn();
	}

	return ksft_get_fail_cnt() ? 1 : 0;
}
