// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <fcntl.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include "kselftest.h"

#ifndef __NR_mq_sendmmsg
#if defined(__alpha__)
#define __NR_mq_sendmmsg 583
#else
#define __NR_mq_sendmmsg 473
#endif
#endif

#define MQ_NAME_PREFIX "/mq_sendmmsg_test_"
#define MQ_MAXMSG 16
#define MAX_MSG_SIZE 128

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

struct send_slot {
	struct msg_attrs desc;
	struct iovec iov;
	const char *payload;
	unsigned int prio;
};

static long mq_sendmmsg(mqd_t mqdes, struct mmsg_attrs *attrs,
					   unsigned int attrs_len, unsigned long start_idx,
					   const struct timespec *timeout)
{
	return syscall(__NR_mq_sendmmsg, mqdes, attrs, attrs_len, start_idx,
				  timeout);
}

static mqd_t open_queue_rw(const char *suffix, unsigned int O_FLAGS, long maxmsg)
{
	static unsigned int seq;
	char name[128];
	struct mq_attr attr = {
		.mq_flags = 0,
		.mq_maxmsg = maxmsg,
		.mq_msgsize = MAX_MSG_SIZE,
	};

	mqd_t mqd;

	snprintf(name, sizeof(name), "%s%s_%d_%u", MQ_NAME_PREFIX, suffix, getpid(), seq++);
	mq_unlink(name);

	mqd = mq_open(name, O_FLAGS, 0600, &attr);
	if (mqd == (mqd_t)-1) {
		ksft_test_result_fail("mq_open(%s) failed: %m\n", name);
		ksft_exit_fail();
	}

	mq_unlink(name);
	return mqd;
}

static void init_send_slot(struct send_slot *slot, const char *payload,
						  unsigned int prio)
{
	memset(slot, 0, sizeof(*slot));
	slot->payload = payload;
	slot->prio = prio;
	slot->desc.msg_len = strlen(payload);
	slot->desc.msg_prio = &slot->prio;
	slot->desc.msg_ptr = (void *)payload;
	slot->iov.iov_base = &slot->desc;
	slot->iov.iov_len = sizeof(slot->desc);
}

static void init_send_slots(struct send_slot *slots,
						   const char *const *payloads,
						   const unsigned int *prios, size_t nr)
{
	size_t i;

	for (i = 0; i < nr; i++)
		init_send_slot(&slots[i], payloads[i], prios[i]);
}

static long batch_send(mqd_t mqd, struct send_slot *slots, size_t nr, int *err,
					  unsigned int start_idx,
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

	return mq_sendmmsg(mqd, &attrs, sizeof(attrs), start_idx, timeout);
}

static long queue_depth(mqd_t mqd)
{
	struct mq_attr attr;

	if (mq_getattr(mqd, &attr))
		return -1;

	return attr.mq_curmsgs;
}

static bool recv_expect(mqd_t mqd, const char *payload, unsigned int prio)
{
	char buf[MAX_MSG_SIZE];
	unsigned int got_prio = 0;
	ssize_t ret;
	size_t len = strlen(payload);

	memset(buf, 0, sizeof(buf));
	ret = mq_receive(mqd, buf, sizeof(buf), &got_prio);
	if (ret < 0)
		return false;
	if ((size_t)ret != len)
		return false;
	if (got_prio != prio)
		return false;
	if (memcmp(buf, payload, len))
		return false;
	return true;
}

static void test_start_idx_obeys_offset(void)
{
	mqd_t mqd = open_queue_rw("offset", O_NONBLOCK | O_RDWR | O_CREAT | O_EXCL, MQ_MAXMSG);
	struct send_slot slots[3];
	const char *payloads[] = { "first", "second", "third" };
	unsigned int prios[] = { 1, 5, 9 };
	long ret;
	int err;

	init_send_slots(slots, payloads, prios, ARRAY_SIZE(slots));
	ret = batch_send(mqd, slots, ARRAY_SIZE(slots), &err, 1, NULL);

	if (ret != 2) {
		ksft_test_result_fail("start_idx skip: ret=%ld expected=2\n", ret);
		goto out;
	}
	if (queue_depth(mqd) != 2) {
		ksft_test_result_fail("start_idx skip: queue depth=%ld expected=2\n",
					queue_depth(mqd));
		goto out;
	}
	if (!recv_expect(mqd, "third", 9) ||
		!recv_expect(mqd, "second", 5)) {
		ksft_test_result_fail("start_idx skip: wrong messages sent\n");
		goto out;
	}

	ksft_test_result_pass("start_idx skips earlier descriptors\n");
out:
	mq_close(mqd);
}

static void test_partial_success_on_null_prio(void)
{
	mqd_t mqd = open_queue_rw("null_prio", O_NONBLOCK | O_RDWR | O_CREAT | O_EXCL, MQ_MAXMSG);
	struct send_slot slots[2];
	const char *payloads[] = { "ok", "bad" };
	unsigned int prios[] = { 7, 3 };
	long ret;
	int err;

	init_send_slots(slots, payloads, prios, ARRAY_SIZE(slots));
	slots[1].desc.msg_prio = NULL;
	ret = batch_send(mqd, slots, ARRAY_SIZE(slots), &err, 0, NULL);

	if (ret != 1) {
		ksft_test_result_fail("partial null prio: ret=%ld expected=1\n", ret);
		goto out;
	}
	if (queue_depth(mqd) != 1 || !recv_expect(mqd, "ok", 7)) {
		ksft_test_result_fail("partial null prio: queue contents mismatch\n");
		goto out;
	}

	ksft_test_result_pass("completed prefix is returned before NULL prio error\n");
out:
	mq_close(mqd);
}

static void test_partial_success_on_bad_desc_len(void)
{
	mqd_t mqd = open_queue_rw("bad_desc", O_NONBLOCK | O_RDWR | O_CREAT | O_EXCL, MQ_MAXMSG);
	struct send_slot slots[2];
	const char *payloads[] = { "ok", "bad" };
	unsigned int prios[] = { 8, 2 };
	long ret;
	int err;

	init_send_slots(slots, payloads, prios, ARRAY_SIZE(slots));
	slots[1].iov.iov_len = sizeof(slots[1].desc) - 1;
	ret = batch_send(mqd, slots, ARRAY_SIZE(slots), &err, 0, NULL);

	if (ret != 1) {
		ksft_test_result_fail("partial bad desc len: ret=%ld expected=1\n", ret);
		goto out;
	}
	if (queue_depth(mqd) != 1 || !recv_expect(mqd, "ok", 8)) {
		ksft_test_result_fail("partial bad desc len: queue contents mismatch\n");
		goto out;
	}

	ksft_test_result_pass("completed prefix is returned before descriptor error\n");
out:
	mq_close(mqd);
}

static void test_partial_success_on_bad_prio_ptr(void)
{
	mqd_t mqd = open_queue_rw("bad_prio_ptr", O_NONBLOCK | O_RDWR | O_CREAT | O_EXCL, MQ_MAXMSG);
	struct send_slot slots[2];
	const char *payloads[] = { "ok", "bad" };
	unsigned int prios[] = { 6, 4 };
	long ret;
	int err;

	init_send_slots(slots, payloads, prios, ARRAY_SIZE(slots));
	slots[1].desc.msg_prio = (void *)1;
	ret = batch_send(mqd, slots, ARRAY_SIZE(slots), &err, 0, NULL);

	if (ret != 1) {
		ksft_test_result_fail("partial bad prio ptr: ret=%ld expected=1\n", ret);
		goto out;
	}
	if (queue_depth(mqd) != 1 || !recv_expect(mqd, "ok", 6)) {
		ksft_test_result_fail("partial bad prio ptr: queue contents mismatch\n");
		goto out;
	}

	ksft_test_result_pass("completed prefix is returned before bad prio pointer fault\n");
out:
	mq_close(mqd);
}

static void test_full_queue_timeout(void)
{
	static const struct timespec expired = { 0, 0 };
	mqd_t mqd = open_queue_rw("full_timeout", O_RDWR | O_CREAT | O_EXCL, 1);
	struct send_slot slot;
	long ret;
	int err;

	init_send_slot(&slot, "first", 1);
	ret = batch_send(mqd, &slot, 1, &err, 0, NULL);
	if (ret != 1)
		ksft_exit_fail_msg("failed to prefill queue\n");

	init_send_slot(&slot, "second", 2);
	ret = batch_send(mqd, &slot, 1, &err, 0, &expired);

	if (ret == -1 && errno == ETIMEDOUT && queue_depth(mqd) == 1)
		ksft_test_result_pass("full queue with expired timeout returns ETIMEDOUT\n");
	else
		ksft_test_result_fail("full queue timeout: ret=%ld errno=%d depth=%ld\n",
					ret, errno, queue_depth(mqd));

	mq_close(mqd);
}

static void test_partial_success_when_queue_fills(void)
{
	static const struct timespec expired = { 0, 0 };
	mqd_t mqd = open_queue_rw("queue_fills", O_NONBLOCK | O_RDWR | O_CREAT | O_EXCL, 2);
	struct send_slot slots[2];
	const char *payloads[] = { "fit", "overflow" };
	unsigned int prios[] = { 1, 2 };
	long ret;
	int err;

	init_send_slot(&slots[0], "prefill", 3);
	ret = batch_send(mqd, &slots[0], 1, &err, 0, NULL);
	if (ret != 1)
		ksft_exit_fail_msg("failed to prefill queue\n");

	init_send_slots(slots, payloads, prios, ARRAY_SIZE(slots));
	ret = batch_send(mqd, slots, ARRAY_SIZE(slots), &err, 0, &expired);

	if (ret != 1) {
		ksft_test_result_fail("queue fills partial: ret=%ld expected=1\n", ret);
		goto out;
	}
	if (queue_depth(mqd) != 2) {
		ksft_test_result_fail("queue fills partial: depth=%ld expected=2\n",
					queue_depth(mqd));
		goto out;
	}
	if (!recv_expect(mqd, "prefill", 3) || !recv_expect(mqd, "fit", 1)) {
		ksft_test_result_fail("queue fills partial: queue contents mismatch\n");
		goto out;
	}

	ksft_test_result_pass("completed operation is returned when queue fills mid-batch\n");
out:
	mq_close(mqd);
}

static const struct {
	const char *name;
	void (*fn)(void);
} tests[] = {
	{ "start_idx obey offset", test_start_idx_obeys_offset },
	{ "partial success on NULL prio", test_partial_success_on_null_prio },
	{ "partial success on bad desc len", test_partial_success_on_bad_desc_len },
	{ "partial success on bad prio pointer", test_partial_success_on_bad_prio_ptr },
	{ "full queue timeout", test_full_queue_timeout },
	{ "partial success when queue fills", test_partial_success_when_queue_fills },
};

int main(void)
{
	size_t i;
	struct send_slot probe;
	long ret;
	int err;

	ksft_print_header();

	init_send_slot(&probe, "probe", 1);
	ret = batch_send((mqd_t)-1, &probe, 1, &err, 0, NULL);
	if (ret == -1 && errno == ENOSYS)
		ksft_exit_skip("mq_sendmmsg syscall not available\n");

	ksft_set_plan(ARRAY_SIZE(tests));

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		ksft_print_msg("[%02zu] %s\n", i + 1, tests[i].name);
		tests[i].fn();
	}

	return ksft_get_fail_cnt() ? 1 : 0;
}
