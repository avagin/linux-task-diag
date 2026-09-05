.. SPDX-License-Identifier: GPL-2.0

=================
MPTCP Repair Mode
=================

Overview
========

MPTCP repair mode is a set of socket options at the ``SOL_MPTCP`` (284) level
designed to enable user-space checkpoint and restoration (C/R) of Multipath TCP
connections (e.g., using tools such as CRIU - Checkpoint/Restore In Userspace).

In traditional TCP, ``TCP_REPAIR`` (under ``SOL_TCP``) allows putting a socket
into a quiesced state where network packets are not transmitted, TCP sequence
numbers and window parameters can be inspected or injected, and unread or
unacknowledged data queues can be saved and restored without generating TCP resets
or spurious handshakes on the wire.

However, Multipath TCP operates at two architectural layers:

1. The connection-level MPTCP socket (``msk``), managing 64-bit Data Sequence
   Numbers (DSN), MPTCP authentication keys, 32-bit connection tokens, and
   data sequence mappings across subflows.
2. One or more underlying TCP subflow sockets (``ssk``), each running a standard
   TCP state machine with 32-bit TCP sequence numbers and Multipath TCP options
   (e.g., ``DSS``, ``MP_CAPABLE``, ``MP_JOIN``).

MPTCP repair mode provides a unified user-space interface to inspect, pause,
reconstruct, and resume both the connection-level MPTCP state and the primary
subflow socket in lockstep.

Privilege Requirements
======================

Calling repair-mode socket options requires the ``CAP_NET_ADMIN`` capability
in the user namespace owning the socket's network namespace
(``sockopt_ns_capable(..., CAP_NET_ADMIN)``).

Repair operations are restricted to established or half-closed connected sockets;
calling them on a listening socket (``sk_state == TCP_LISTEN``) returns
``-EPERM``.

Sockets API
===========

MPTCP repair mode is controlled through ``setsockopt()`` and ``getsockopt()``
at level ``SOL_MPTCP`` (284).

The following options are supported:

========================  ================  ====================================
Option Name               Option Number     Purpose
========================  ================  ====================================
``MPTCP_REPAIR``          10                Enter or exit MPTCP repair mode
``MPTCP_REPAIR_KEYS``     11                Dump / restore keys, token, and flags
``MPTCP_REPAIR_SEQ``      12                Dump / restore 64-bit DSNs and state
``MPTCP_REPAIR_SUBFLOW``  13                Dump / restore subflow parameters
``MPTCP_REPAIR_QUEUE``    14                Select queue for dump or injection
========================  ================  ====================================

MPTCP_REPAIR
------------

``MPTCP_REPAIR`` takes an integer argument that switches repair mode on or off:

.. code-block:: c

    int val = MPTCP_REPAIR_ON;
    setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR, &val, sizeof(val));

Supported values:

* ``MPTCP_REPAIR_ON`` (1):
  Puts the MPTCP socket and all active subflows into repair mode. While in
  repair mode:

  - Outgoing network traffic is suspended; timer-based retransmissions and
    connection timeouts are stopped (``mptcp_stop_tout_timer()``).
  - Subflow TCP sockets enter TCP repair mode (``tp->repair = 1``).
  - Address rebinding is permitted using ``SK_FORCE_REUSE``.
  - If the socket is closed while in repair mode, no ``RST`` or ``FIN`` packets
    are emitted to the remote peer, leaving the remote connection intact for
    subsequent migration or resumption.
  - The active repair queue is initialized to ``MPTCP_NO_QUEUE``.

* ``MPTCP_REPAIR_OFF`` (0):
  Exits repair mode on the MPTCP socket and all active subflows:

  - Normal packet scheduling and retransmission timers resume.
  - Subflow TCP sockets exit TCP repair mode (``tp->repair = 0``).
  - Any pending data queued during repair is pushed to active subflows
    (``__mptcp_push_pending()``).
  - If the socket was restored in a write-closed state (e.g., ``TCP_FIN_WAIT1``,
    ``TCP_LAST_ACK``, or ``TCP_CLOSING``) where DATA_FIN has not yet been
    acknowledged, ``mptcp_check_send_data_fin()`` is triggered to transmit
    the DATA_FIN to the peer.

* ``MPTCP_REPAIR_OFF_NO_WP`` (2):
  Exits repair mode quietly without triggering window probes or immediate
  retransmissions (i.e. omitting ``__mptcp_push_pending()`` and
  ``mptcp_check_send_data_fin()``).

  This mirrors the functionality of ``TCP_REPAIR_OFF_NO_WP`` in standard TCP
  and is required for several key checkpoint/restore scenarios:

  - **Network-Locked Live Migration**:
    During live container migration, network locks (e.g., netfilter/nftables
    rules) block packet delivery while state is transferred. Sockets must be
    taken out of repair mode *before* releasing network locks and resuming
    application processes. If the kernel proactively pushes data or emits
    probes on repair exit, those packets are dropped by the network lock or
    sent before network interfaces and ARP/routing tables are fully initialized.
  - **Non-Destructive Inspection and Checkpoint Abort**:
    If a checkpoint operation is aborted or during a pre-dump phase where the
    original process continues execution, a socket that was temporarily placed
    into repair mode must be unpaused without wire side-effects. Using
    ``MPTCP_REPAIR_OFF_NO_WP`` unfreezes the socket without sending spurious
    probes, duplicate ACKs, or premature retransmissions to the remote peer.
  - **Simultaneous Multi-Endpoint Migration**:
    When migrating both endpoints of a connection (e.g., inter-container or
    intra-pod migration), one endpoint will inevitably be restored before the
    other. If the first endpoint exits repair mode with proactive transmission,
    its packets will arrive before the second endpoint has exited repair mode,
    potentially causing connection resets.
  - **Zero-Window and Flow-Control Preservation**:
    If a socket was checkpointed with a closed receive or send window,
    avoiding immediate transmission prevents premature probe floods before
    the application has consumed buffered data.

``getsockopt()`` with ``MPTCP_REPAIR`` returns an integer indicating whether
repair mode is currently active (``MPTCP_REPAIR_ON`` or ``MPTCP_REPAIR_OFF``).

MPTCP_REPAIR_KEYS
-----------------

``MPTCP_REPAIR_KEYS`` inspects or configures the 64-bit cryptographic keys and
32-bit MPTCP token that authenticate the connection and its subflows.

.. code-block:: c

    struct mptcp_repair_keys {
        __u64   local_key;
        __u64   remote_key;
        __u32   token;
        __u32   flags;
    };

Fields:

* ``local_key``:
  The 64-bit key generated by the local endpoint during connection setup.
* ``remote_key``:
  The 64-bit key advertised by the remote peer during connection setup.
* ``token``:
  The 32-bit MPTCP token derived from the SHA-256 hash of ``local_key``.
* ``flags``:
  Bitmask of connection properties:

  - ``MPTCP_KEY_FLAG_CSUM_ENABLED`` (bit 0): DSS checksum is enabled.
  - ``MPTCP_KEY_FLAG_64BIT_ACK`` (bit 1): 64-bit Data Sequence Number ACKs are used.
  - ``MPTCP_KEY_FLAG_FALLBACK`` (bit 2): The connection has fallen back to plain TCP.

Semantics:

* ``getsockopt(..., MPTCP_REPAIR_KEYS, &keys, &optlen)``:
  Returns the keys and token associated with the socket.
* ``setsockopt(..., MPTCP_REPAIR_KEYS, &keys, sizeof(keys))``:
  Restores the keys and registers the token into the kernel token bucket.
  If the socket already has a token registered, the call returns ``-EBUSY``.
  If the token conflicts with an existing active token, it returns ``-EADDRINUSE``.

MPTCP_REPAIR_SEQ
----------------

``MPTCP_REPAIR_SEQ`` inspects or configures the 64-bit Data Sequence Numbers (DSN)
and socket-level state.

.. code-block:: c

    struct mptcp_repair_seq {
        __u64   write_seq;
        __u64   snd_nxt;
        __u64   snd_una;
        __u64   rcv_nxt;
        __u64   rcv_wnd_sent;
        __u64   rcv_data_fin_seq;
        __u32   mptcp_state;
        __u32   flags;
    };

Fields:

* ``write_seq``:
  The next byte offset to be allocated in the MPTCP transmit buffer.
* ``snd_nxt``:
  The next 64-bit data sequence number to transmit on the wire.
* ``snd_una``:
  The earliest unacknowledged 64-bit data sequence number.
* ``rcv_nxt``:
  The next expected 64-bit data sequence number from the peer (maps to
  ``msk->ack_seq``).
* ``rcv_wnd_sent``:
  The left edge of the advertised receive window sent to the peer.
* ``rcv_data_fin_seq``:
  The sequence number of the remote peer's DATA_FIN (if received).
* ``mptcp_state``:
  The connection state (e.g., ``TCP_ESTABLISHED``, ``TCP_CLOSE_WAIT``,
  ``TCP_FIN_WAIT1``, ``TCP_FIN_WAIT2``, ``TCP_LAST_ACK``, ``TCP_CLOSING``).
* ``flags``:
  Bitmask of sequence state flags:

  - ``MPTCP_SEQ_FLAG_RCV_DATA_FIN`` (bit 0): Remote peer sent DATA_FIN.
  - ``MPTCP_SEQ_FLAG_SND_DATA_FIN_ENABLE`` (bit 1): Local endpoint initiated
    shutdown / sent DATA_FIN.
  - ``MPTCP_SEQ_FLAG_ALLOW_INFINITE_FALLBACK`` (bit 2): Infinite fallback allowed.

Semantics:

* ``getsockopt(..., MPTCP_REPAIR_SEQ, &seq, &optlen)``:
  Extracts current sequence numbers, state, and flags.
* ``setsockopt(..., MPTCP_REPAIR_SEQ, &seq, sizeof(seq))``:
  Restores sequence numbers. If ``mptcp_state`` is non-zero, sets the socket
  state and adjusts shutdown masks (e.g., setting ``RCV_SHUTDOWN`` for
  ``TCP_CLOSE_WAIT``, ``SEND_SHUTDOWN`` for ``TCP_FIN_WAIT1``/``TCP_FIN_WAIT2``,
  or ``SHUTDOWN_MASK`` for ``TCP_LAST_ACK``/``TCP_CLOSING``).

MPTCP_REPAIR_SUBFLOW
--------------------

``MPTCP_REPAIR_SUBFLOW`` inspects or reconstructs the primary TCP subflow
underlying the MPTCP connection.

.. code-block:: c

    struct mptcp_repair_subflow {
        struct mptcp_subflow_addrs addrs;
        __u32   subflow_id;
        __u32   snd_una;
        __u32   snd_nxt;
        __u32   rcv_nxt;
        __u32   snd_wnd;
        __u32   rcv_wnd;
        __u32   mss_clamp;
        __u32   ts_recent;
        __u32   ts_recent_stamp;
        __u32   tsoffset;
        __u16   flags;
        __u8    snd_wscale;
        __u8    rcv_wscale;
        __u8    local_id;
        __u8    remote_id;
        __u16   reserved;
        __u64   idsn;
        __u64   map_seq;
        __u32   map_subflow_seq;
        __u32   ssn_offset;
        __u32   rel_write_seq;
        __u32   reserved2;
    };

Fields:

* ``addrs``:
  Local and remote socket addresses (``sin_local``/``sin6_local`` and
  ``sin_remote``/``sin6_remote``) defined by ``struct mptcp_subflow_addrs``.
* ``subflow_id``:
  The subflow identifier.
* ``snd_una``, ``snd_nxt``, ``rcv_nxt``:
  32-bit TCP sequence numbers of the subflow.
* ``snd_wnd``, ``rcv_wnd``:
  TCP advertised window values.
* ``mss_clamp``, ``ts_recent``, ``ts_recent_stamp``, ``tsoffset``:
  TCP negotiation options and timestamp parameters.
* ``flags``:
  Bitmask of subflow properties:

  - ``MPTCP_SUBFLOW_REPAIR_FLAG_SACK_OK`` (bit 0): TCP SACK permitted.
  - ``MPTCP_SUBFLOW_REPAIR_FLAG_TIMESTAMPS`` (bit 1): TCP timestamps negotiated.
  - ``MPTCP_SUBFLOW_REPAIR_FLAG_BACKUP`` (bit 2): Subflow is a backup path.
  - ``MPTCP_SUBFLOW_REPAIR_FLAG_JOIN`` (bit 3): Subflow was created via MP_JOIN.

* ``snd_wscale``, ``rcv_wscale``:
  TCP window scaling factors.
* ``local_id``, ``remote_id``:
  MPTCP address IDs.
* ``idsn``:
  Initial Data Sequence Number for this subflow.
* ``map_seq``, ``map_subflow_seq``, ``ssn_offset``:
  DSS mapping parameters mapping subflow sequence space to MPTCP DSN space.
* ``rel_write_seq``:
  Subflow relative write sequence number. On dump, this value is adjusted
  to reflect actual wire transmission progress:

  .. code-block:: c

      sf.rel_write_seq = subflow->rel_write_seq - (tp->write_seq - tp->snd_nxt);

Semantics:

* ``getsockopt(..., MPTCP_REPAIR_SUBFLOW, &sf, &optlen)``:
  Extracts subflow configuration and sequence status.
* ``setsockopt(..., MPTCP_REPAIR_SUBFLOW, &sf, sizeof(sf))``:
  Creates a new subflow socket, grafts it to the MPTCP socket, binds the local
  address, and executes a repair-mode connect to the remote address without
  sending SYN packets. Restores subflow TCP sequence numbers, window parameters,
  and MPTCP subflow mapping structures.

MPTCP_REPAIR_QUEUE
------------------

``MPTCP_REPAIR_QUEUE`` selects which queue is targeted for inspection or data
injection:

.. code-block:: c

    int queue = MPTCP_SEND_QUEUE;
    setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_QUEUE, &queue, sizeof(queue));

Supported queues:

* ``MPTCP_SEND_QUEUE`` (0):
  Targets the MPTCP send buffer and unacknowledged retransmission queue
  (``msk->rtx_queue``).
* ``MPTCP_RECV_QUEUE`` (1):
  Targets the MPTCP receive queue (``sk->sk_receive_queue``).
* ``MPTCP_NO_QUEUE`` (2):
  Deselects any queue. Calling ``send()`` while ``MPTCP_NO_QUEUE`` is active
  returns ``-EINVAL``.

Dumping Queues:
  When repair mode is active and a queue is selected:

  - Calling ``recv(fd, buf, len, MSG_PEEK)`` with ``MPTCP_SEND_QUEUE`` peeks
    unacknowledged in-flight data fragments directly from ``msk->rtx_queue``.
  - Calling ``recv(fd, buf, len, MSG_PEEK)`` with ``MPTCP_RECV_QUEUE`` peeks
    unread payload data from ``sk->sk_receive_queue``.

Restoring Queues:
  When repair mode is active and a queue is selected:

  - Calling ``send(fd, buf, len, 0)`` with ``MPTCP_SEND_QUEUE`` injects
    payload data into ``msk->rtx_queue``. The kernel carves data fragments and
    initializes their ``already_sent`` counters based on ``dfrag->data_seq``
    relative to ``msk->snd_nxt``:

    * If ``data_seq + len <= snd_nxt``: Marked as already sent (in flight).
    * If ``data_seq < snd_nxt``: Partially sent.
    * If ``data_seq >= snd_nxt``: Unsent data.

  - Calling ``send(fd, buf, len, 0)`` with ``MPTCP_RECV_QUEUE`` injects
    payload data into ``sk->sk_receive_queue``, constructs MPTCP DSS mappings
    (``map_seq`` and ``end_seq``), and updates ``msk->ack_seq``.

Related Socket Options
======================

In addition to ``SOL_MPTCP`` repair options, checkpoint/restore tools rely on
standard socket options that have been adapted to support MPTCP:

* ``SO_REUSEADDR`` / ``SO_REUSEPORT``:
  Can be set on connected MPTCP sockets to allow binding restored subflows to
  the original ports.
* ``SO_BINDTODEVICE`` / ``SO_BINDTOIFINDEX``:
  Can be set on connected MPTCP sockets and propagates to active subflows while
  invalidating destination caches (``sk_dst_reset()``).
* ``SO_BUF_LOCK``:
  Allows setting and preserving ``SOCK_RCVBUF_LOCK`` and ``SOCK_SNDBUF_LOCK``
  on MPTCP and subflow sockets, preventing automatic buffer tuning from
  overwriting restored buffer limits.
* ``IP_TTL``, ``IP_PKTINFO``, ``IPV6_RECVPKTINFO``:
  Synchronize IP-level packet options from the MPTCP socket to underlying
  subflows.

Checkpoint & Restore Procedure
==============================

Checkpoint Procedure
--------------------

1. **Pause Connection**:
   Put the socket into repair mode:

   .. code-block:: c

       int val = MPTCP_REPAIR_ON;
       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR, &val, sizeof(val));

2. **Dump Keys**:

   .. code-block:: c

       struct mptcp_repair_keys keys;
       socklen_t optlen = sizeof(keys);
       getsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_KEYS, &keys, &optlen);

3. **Dump Sequences & State**:

   .. code-block:: c

       struct mptcp_repair_seq seq;
       optlen = sizeof(seq);
       getsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_SEQ, &seq, &optlen);

4. **Dump Subflow State**:

   .. code-block:: c

       struct mptcp_repair_subflow sf;
       optlen = sizeof(sf);
       getsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_SUBFLOW, &sf, &optlen);

5. **Dump Queues**:
   Determine queue lengths via ``ioctl(fd, SIOCOUTQ)`` and ``ioctl(fd, SIOCINQ)``.
   Set queue and peek data:

   .. code-block:: c

       int q = MPTCP_RECV_QUEUE;
       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_QUEUE, &q, sizeof(q));
       recv(fd, recv_buf, inq_len, MSG_PEEK);

       q = MPTCP_SEND_QUEUE;
       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_QUEUE, &q, sizeof(q));
       recv(fd, send_buf, outq_len, MSG_PEEK);

6. **Close Socket**:
   Close the socket while still in repair mode. No RST or FIN is sent.

Restore Procedure
-----------------

1. **Create Socket**:

   .. code-block:: c

       int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);

2. **Enable Repair Mode**:

   .. code-block:: c

       int val = MPTCP_REPAIR_ON;
       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR, &val, sizeof(val));

3. **Restore Keys**:

   .. code-block:: c

       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_KEYS, &keys, sizeof(keys));

4. **Restore Sequences & State**:
   If unacknowledged data exists (``outq_len > 0``), ``write_seq`` is temporarily
   initialized to ``snd_una`` so injected send-queue fragments begin at ``snd_una``.
   Likewise, ``rcv_nxt`` is temporarily rewound by ``inq_len`` during receive
   queue injection.

   .. code-block:: c

       __u64 target_snd_nxt = (outq_len && sf.idsn) ?
                              (sf.idsn + sf.rel_write_seq) : seq.snd_nxt;

       struct mptcp_repair_seq rseq = seq;
       rseq.write_seq = outq_len ? seq.snd_una : seq.write_seq;
       rseq.snd_nxt = target_snd_nxt;
       rseq.rcv_nxt = inq_len ? (seq.rcv_nxt - inq_len) : seq.rcv_nxt;
       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_SEQ, &rseq, sizeof(rseq));

5. **Restore Subflow**:

   .. code-block:: c

       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_SUBFLOW, &sf, sizeof(sf));

6. **Inject Queues**:

   .. code-block:: c

       if (inq_len) {
           int q = MPTCP_RECV_QUEUE;
           setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_QUEUE, &q, sizeof(q));
           send(fd, recv_buf, inq_len, 0);
       }

       if (outq_len) {
           int q = MPTCP_SEND_QUEUE;
           setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_QUEUE, &q, sizeof(q));
           send(fd, send_buf, outq_len, 0);
       }

7. **Re-sync Sequence Numbers**:
   If data queues were restored, re-sync sequence numbers to final values:

   .. code-block:: c

       if (inq_len || outq_len) {
           rseq.write_seq = seq.write_seq;
           rseq.snd_nxt = target_snd_nxt;
           rseq.rcv_nxt = seq.rcv_nxt;
           setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR_SEQ, &rseq, sizeof(rseq));
       }

8. **Restore Socket Options**:
   Restore buffer locks, timeouts, TOS, and mark.

9. **Exit Repair Mode**:

   .. code-block:: c

       val = MPTCP_REPAIR_OFF;
       setsockopt(fd, SOL_MPTCP, MPTCP_REPAIR, &val, sizeof(val));

   The connection resumes data transfer seamlessly.

References
==========

* Kernel selftest: ``tools/testing/selftests/net/mptcp/mptcp_repair.c``
* CRIU implementation: ``soccr/soccr.c`` and ``criu/sk-mptcp.c``
