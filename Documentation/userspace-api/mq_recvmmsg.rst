.. SPDX-License-Identifier: GPL-2.0

mq_recvmmsg system call
=============================

This document describes the mq_recvmmsg() system call. It provides
an overview of the feature, interface specification, design, and
test specification.

Contents
--------

        1) Overview
        2) Functional Specification
        3) Design
        4) Test Specification

1) Overview
-----------

POSIX message queues on Linux provide mq_timedreceive() for consuming
messages from a queue.This interface requires the caller to pass the
message buffer, length and priority pointer as individual arguments to
the system call. This imposes a fixed calling convention that cannot be
extended without breaking the ABI.

mq_recvmmsg() introduces a new system call entry point that accepts
message buffer parameters via a struct argument inside a iovec rather
than as individual syscall arguments.

One 64-bit variant is provided with compat handling:
    mq_recvmmsg()

2) Functional Specification
---------------------------

NAME
        mq_recvmmsg - receive or peek as a batch of messages from a
        POSIX message queue

SYNOPSIS

.. code-block:: c

        #include <mqueue.h>

        struct mq_msg_attrs {
	        __kernel_size_t msg_len;
	        unsigned int __user *msg_prio;
	        void __user *msg_ptr;
        };

        struct mq_mmsg_attrs {
	        struct iovec __user *msg_attrs_vec;
	        __kernel_size_t vlen;
                int *ret;
        };

        ssize_t mq_recvmmsg(mqd_t mqdes, struct mq_mmsg_attrs *attrs,
                           unsigned int attrs_len, unsigned int flags,
                           unsigned long start_idx,
                           const struct __kernel_timespec *abs_timeout);


DESCRIPTION
        mq_recvmmsg() receives or peeks as a batch of messages from the
        message queue referred to by the descriptor mqdes.

        The flags argument controls receive behavior. The following
        flag is defined:

        ``MQ_PEEK``
                Copies the message into struct mq_msg_attrs without removing it from
                the queue. The queue is not modified.

        ``MQ_RECV``
                Copies and consumes messages from the queue up to the count specified
                by vlen.

        The start_idx argument specifies which message to begin operating on within
        the priority-ordered queue, relative to the requested batch range.

        The abs_timeout argument specifies an absolute timeout for the entire batch
        operation. When MQ_PEEK is set, abs_timeout is ignored, since peek is a
        non-blocking snapshot operation. When MQ_PEEK is not set, abs_timeout be the
        timeout for the entire batch.

RETURN VALUE
        On success, returns the number of messages copied into user-space.

        On failure, the function returns an error directly without writing
        to the ret field of struct mq_mmsg_attrs in two situations:

        * An error occurred before a single message was successfully copied to user space.
        * new error occurred in writing error itself in ret field of struct mq_mmsg_attrs
        after partial success before completing batch.


ERRORS
        ``EAGAIN``
                Queue is empty and MQ_PEEK is set. Peek is always
                non-blocking and returns immediately on empty queue.

        ``EBADF``
                mqdes is not a valid message queue descriptor open
                for reading.

        ``EFAULT``
                attrs, msg_ptr, msg_prio, or abs_timeout points to
                an invalid address or any invalid address.

        ``EINVAL``
                The batch size is too large, start_idx is inconsistent with the
                current queue state, flags contains an unrecognized value, or contradictory
                flags were passed.

        ``EMSGSIZE``
                msg_len is less than the mq_msgsize attribute of
                the queue.

        ``E2BIG``
                attrs_len is greater than PAGE_SIZE.

        ``ETIMEDOUT``
                Pop path only. The call timed out before a message
                became available. Never returned on peek path.

        ``ENODATA``
                No valid message exists matching the given parameters.

        ``ENOMEM``
                 Insufficient memory to complete the operation.

3) Design
---------

3.1 Struct-based argument passing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The message buffer parameters (msg_ptr, msg_len, msg_prio) are consolidated
into struct mq_msg_attrs rather than passed as individual syscall arguments.
These are then referenced via struct mq_mmsg_attrs, which contains an
iovec msg_attrs_vec and a vlen count. The base of msg_attrs_vec holds individual
mq_msg_attrs entries, and length store struct mq_msg_attrs size.


3.2 Compat handling
~~~~~~~~~~~~~~~~~~~~

.. code-block:: c

        struct compat_msg_attrs {
	        compat_size_t msg_len;
	        compat_uptr_t msg_prio;
	        compat_uptr_t msg_ptr;
        };

        struct compat_mq_mmsg_attrs {
	        compat_uptr_t msg_attrs_vec;
	        compat_size_t vlen;
	        compat_uptr_t ret;
        };


The compat entry point performs the necessary conversions before
calling the shared do_mq_recvmmsg() implementation.

3.3 Peek implementation
~~~~~~~~~~~~~~~~~~~~~~~~

When MQ_PEEK is set, the implementation locates the target message
in the priority tree but does not remove it. Two locks are taken:
the first confirms a message exists before any allocation is
attempted and retrieve required buffer size to do memory allocation
outside of critical section, avoiding allocation on empty queues.
The second protects the kernel temporary buffer copy operation.

3.4 start_idx argument
~~~~~~~~~~~~~~~~~~~

The start_idx argument specifies which message to begin operating on within the queue.
If a batch request fails partway through, user space is expected to supply an updated
start_idx reflecting the number of messages already handled, failing to do so may result
in unexpected message delivery.

4) Test Specification
---------------------

Tests for mq_recvmmsg() should cover the following:

1) Basic receive and peek — Verify that without MQ_PEEK the message is consumed and
the queue depth decreases by one. Verify that the message body, priority, and return
value are correct.

2) Offset and boundary handling — Verify that the implementation correctly respects
batch size and start_idx boundary conditions.

3) Partial success — Verify that when an error occurs mid-batch, the call returns
successfully with the count of messages processed so far, rather than discarding partial
results.





