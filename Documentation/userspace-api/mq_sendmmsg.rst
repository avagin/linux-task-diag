.. SPDX-License-Identifier: GPL-2.0

mq_sendmmsg system call
=============================

This document describes the mq_sendmmsg() system call. It provides
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

POSIX message queues on Linux provide mq_timedsend() for storing
messages in a queue one at time.

mq_sendmmsg() introduces a new system call entry point that take
messages in batch and store in queues.

One 64-bit variant is provided with compat handling:
    mq_sendmmsg()

2) Functional Specification
---------------------------

NAME
        mq_sendmmsg - send messages as a batch to queues.

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

        ssize_t mq_sendmmsg(mqd_t mqdes, struct mq_mmsg_attrs *attrs,
                           unsigned int attrs_len, unsigned long start_idx,
                           const struct __kernel_timespec *abs_timeout);


DESCRIPTION
        mq_sendmmsg() send messages in batch to message queue referred
        to by the descriptor mqdes.

        The start_idx argument specifies which message to begin operating on within
        relative to the requested batch range.

        The abs_timeout argument specifies an absolute timeout for the entire batch
        operation.

RETURN VALUE
        On success, returns the number of messages stored into the queues.

        On failure, the function returns an error directly without writing
        to the ret field of struct mq_mmsg_attrs in two situations:

        * An error occurred before a single message was successfully stored into the queues.
        * A new error occurred in writing error itself in ret field of struct mq_mmsg_attrs
        after partial success before completing batch.


ERRORS
        ``EAGAIN``
                Queue is not empty.

        ``EBADF``
                mqdes is not a valid message queue descriptor open
                for reading.

        ``EFAULT``
                attrs, msg_ptr, msg_prio, or abs_timeout points to
                an invalid address.

        ``EINVAL``
                The batch size is too large, start_idx is inconsistent with the
                current queue state.

        ``EMSGSIZE``
                msg_len is greater than the mq_msgsize attribute of
                the queue.

        ``E2BIG``
                attrs_len is greater than PAGE_SIZE.

        ``ETIMEDOUT``
                The call timed out before a space became available to store
                new messages.

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
calling the shared do_mq_sendmmsg() implementation.


3.3 start_idx argument
~~~~~~~~~~~~~~~~~~~

The start_idx argument specifies which message to begin operating on within the queue.
If a batch request fails partway through, user space is expected to supply an updated
start_idx reflecting the number of messages already handled, failing to do so may result
in duplicate message passed to queues.

4) Test Specification
---------------------

Tests for mq_sendmmsg() should cover the following:

1) Offset and boundary handling — Verify that the implementation work correctly as per
batch size and start_idx boundary conditions.

2) Partial success — Verify that when an error occurs mid-batch, the call returns
successfully with the count of messages processed so far.

3) Batch Timeout - Verify batch absolute deadline return expected value when
queue is completely full.



