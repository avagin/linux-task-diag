/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRBD_TRANSPORT_H
#define DRBD_TRANSPORT_H

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/socket.h>

/* Whenever touch this file in a non-trivial way, increase the
   DRBD_TRANSPORT_API_VERSION
   So that transport compiled against an older version of this
   header will no longer load in a module that assumes a newer
   version. */
#define DRBD_TRANSPORT_API_VERSION 21

/* MSG_MSG_DONTROUTE and MSG_PROBE are not used by DRBD. I.e.
   we can reuse these flags for our purposes */
#define CALLER_BUFFER  MSG_DONTROUTE
#define GROW_BUFFER    MSG_PROBE

/*
 * gfp_mask for allocating memory with no write-out.
 *
 * When drbd allocates memory on behalf of the peer, we prevent it from causing
 * write-out because in a criss-cross setup, the write-out could lead to memory
 * pressure on the peer, eventually leading to deadlock.
 */
#define GFP_TRY	(__GFP_HIGHMEM | __GFP_NOWARN | __GFP_RECLAIM)

#define tr_printk(level, transport, fmt, args...)  ({		\
	rcu_read_lock();					\
	printk(level "drbd %s %s:%s: " fmt,			\
	       (transport)->log_prefix,				\
	       (transport)->class->name,			\
	       rcu_dereference((transport)->net_conf)->name,	\
	       ## args);					\
	rcu_read_unlock();					\
	})

#define tr_err(transport, fmt, args...) \
	tr_printk(KERN_ERR, transport, fmt, ## args)
#define tr_warn(transport, fmt, args...) \
	tr_printk(KERN_WARNING, transport, fmt, ## args)
#define tr_notice(transport, fmt, args...) \
	tr_printk(KERN_NOTICE, transport, fmt, ## args)
#define tr_info(transport, fmt, args...) \
	tr_printk(KERN_INFO, transport, fmt, ## args)

#define TR_ASSERT(x, exp)							\
	do {									\
		if (!(exp))							\
			tr_err(x, "ASSERTION %s FAILED in %s\n",	\
				 #exp, __func__);				\
	} while (0)

struct drbd_resource;
struct drbd_listener;
struct drbd_transport;

enum drbd_stream {
	DATA_STREAM,
	CONTROL_STREAM
};

enum drbd_tr_hints {
	CORK,
	UNCORK,
	NODELAY,
	NOSPACE,
	QUICKACK
};

enum { /* bits in the flags word */
	NET_CONGESTED,		/* The data socket is congested */
	RESOLVE_CONFLICTS,	/* Set on one node, cleared on the peer! */
};

enum drbd_tr_free_op {
	CLOSE_CONNECTION,
	DESTROY_TRANSPORT
};

enum drbd_tr_event {
	CLOSED_BY_PEER,
	TIMEOUT,
};

enum drbd_tr_path_flag {
	TR_ESTABLISHED, /* updated by the transport */
	TR_UNREGISTERED,
	TR_TRANSPORT_PRIVATE = 32, /* flags starting here are used exclusively by the transport */
};

/* A transport might wrap its own data structure around this. Having
   this base class as its first member. */
struct drbd_path {
	struct sockaddr_storage my_addr;
	struct sockaddr_storage peer_addr;

	struct kref kref;

	struct net *net;
	int my_addr_len;
	int peer_addr_len;
	unsigned long flags;

	struct drbd_transport *transport;
	struct list_head list; /* paths of a connection */
	struct list_head listener_link; /* paths waiting for an incoming connection,
					   head is in a drbd_listener */
	struct drbd_listener *listener;

	struct rcu_head rcu;
};

/* Each transport implementation should embed a struct drbd_transport
   into it's instance data structure. */
struct drbd_transport {
	struct drbd_transport_class *class;

	struct list_head paths;

	const char *log_prefix;		/* resource name */
	struct net_conf __rcu *net_conf;	/* content protected by rcu */

	/* These members are intended to be updated by the transport: */
	unsigned int ko_count;
	unsigned long flags;
};

struct drbd_transport_stats {
	int unread_received;
	int unacked_send;
	int send_buffer_size;
	int send_buffer_used;
};

/* argument to ->recv_pages() */
struct drbd_page_chain_head {
	struct page *head;
	unsigned int nr_pages;
};

struct drbd_const_buffer {
	const u8 *buffer;
	unsigned int avail;
};

/**
 * struct drbd_transport_ops - Operations implemented by the transport.
 *
 * The user of this API guarantees that all of the following will be exclusive
 * with respect to each other for a given transport instance:
 * * init()
 * * free()
 * * prepare_connect()
 * * finish_connect()
 * * add_path() and the subsequent list_add_tail_rcu() for the paths list
 * * may_remove_path() and the subsequent list_del_rcu() for the paths list
 *
 * The connection sequence is as follows:
 * 1. prepare_connect(), with the above exclusivity guarantee
 * 2. connect(), this may take a long time
 * 3. finish_connect(), with the above exclusivity guarantee
 */
struct drbd_transport_ops {
	int (*init)(struct drbd_transport *);
	void (*free)(struct drbd_transport *, enum drbd_tr_free_op free_op);
	int (*init_listener)(struct drbd_transport *, const struct sockaddr *, struct net *net,
			     struct drbd_listener *);
	void (*release_listener)(struct drbd_listener *);
	int (*prepare_connect)(struct drbd_transport *);
	int (*connect)(struct drbd_transport *);
	void (*finish_connect)(struct drbd_transport *);

/**
 * recv() - Receive data via the transport
 * @transport:	The transport to use
 * @stream:	The stream within the transport to use. Ether DATA_STREAM or CONTROL_STREAM
 * @buf:	The function will place here the pointer to the data area
 * @size:	Number of byte to receive
 * @msg_flags:	Bitmask of CALLER_BUFFER, GROW_BUFFER and MSG_DONTWAIT
 *
 * recv() returns the requests data in a buffer (owned by the transport).
 * You may pass MSG_DONTWAIT as flags.  Usually with the next call to recv()
 * or recv_pages() on the same stream, the buffer may no longer be accessed
 * by the caller. I.e. it is reclaimed by the transport.
 *
 * If the transport was not capable of fulfilling the complete "wish" of the
 * caller (that means it returned a smaller size that size), the caller may
 * call recv() again with the flag GROW_BUFFER, and *buf as returned by the
 * previous call.
 * Note1: This can happen if MSG_DONTWAIT was used, or if a receive timeout
 *	was we with set_rcvtimeo().
 * Note2: recv() is free to re-locate the buffer in such a call. I.e. to
 *	modify *buf. Then it copies the content received so far to the new
 *	memory location.
 *
 * Last not least the caller may also pass an arbitrary pointer in *buf with
 * the CALLER_BUFFER flag. This is expected to be used for small amounts
 * of data only
 *
 * Upon success the function returns the bytes read. Upon error the return
 * code is negative. A 0 indicates that the socket was closed by the remote
 * side.
 */
	int (*recv)(struct drbd_transport *, enum drbd_stream, void **buf, size_t size, int flags);

/**
 * recv_pages() - Receive bulk data via the transport's DATA_STREAM
 * @peer_device: Identify the transport and the device
 * @page_chain:	Here recv_pages() will place the page chain head and length
 * @size:	Number of bytes to receive
 *
 * recv_pages() will return the requested amount of data from DATA_STREAM,
 * and place it into pages allocated with drbd_alloc_pages().
 *
 * Upon success the function returns 0. Upon error the function returns a
 * negative value
 */
	int (*recv_pages)(struct drbd_transport *, struct drbd_page_chain_head *, size_t size);

	void (*stats)(struct drbd_transport *, struct drbd_transport_stats *stats);
/**
 * net_conf_change() - Notify about changed network configuration on the transport.
 * @new_net_conf: The new network configuration that should be applied.
 *
 * net_conf_change() is called in the context of either the initial creation of the connection,
 * or when the net_conf is changed via netlink. Note that assignment of the net_conf to the
 * transport object happens after this function is called.
 *
 * On a negative (error) return value, it is expected that any changes are reverted and
 * the old net_conf (if any) is still in effect.
 *
 * Upon success the function return 0. Upon error the function returns a negative value.
 */
	int (*net_conf_change)(struct drbd_transport *, struct net_conf *new_net_conf);
	void (*set_rcvtimeo)(struct drbd_transport *, enum drbd_stream, long timeout);
	long (*get_rcvtimeo)(struct drbd_transport *, enum drbd_stream);
	int (*send_page)(struct drbd_transport *, enum drbd_stream, struct page *,
			 int offset, size_t size, unsigned msg_flags);
	int (*send_zc_bio)(struct drbd_transport *, struct bio *bio);
	bool (*stream_ok)(struct drbd_transport *, enum drbd_stream);
	bool (*hint)(struct drbd_transport *, enum drbd_stream, enum drbd_tr_hints hint);
	void (*debugfs_show)(struct drbd_transport *, struct seq_file *m);

/**
 * add_path() - Prepare path to be added
 * @path: The path that is being added
 *
 * Called before the path is added to the paths list.
 *
 * Return: 0 if path may be added, error code otherwise.
 */
	int (*add_path)(struct drbd_path *path);

/**
 * may_remove_path() - Query whether path may currently be removed
 * @path: The path to be removed
 *
 * Return: true is path may be removed, false otherwise.
 */
	bool (*may_remove_path)(struct drbd_path *path);

/**
 * remove_path() - Clear up after path removal
 * @path: The path that is being removed
 *
 * Clear up a path that is being removed. Called after the path has been
 * removed from the list and all kref references have been put.
 */
	void (*remove_path)(struct drbd_path *path);
};

struct drbd_transport_class {
	const char *name;
	const int instance_size;
	const int path_instance_size;
	const int listener_instance_size;
	struct drbd_transport_ops ops;

	struct module *module;

	struct list_head list;
};


/* An "abstract base class" for transport implementations. I.e. it
   should be embedded into a transport specific representation of a
   listening "socket" */
struct drbd_listener {
	struct kref kref;
	struct drbd_resource *resource;
	struct drbd_transport_class *transport_class;
	struct list_head list; /* link for resource->listeners */
	struct list_head waiters; /* list head for paths */
	spinlock_t waiters_lock;
	int pending_accepts;
	struct sockaddr_storage listen_addr;
	struct completion ready;
	int err;
};

/* drbd_main.c */
void drbd_destroy_path(struct kref *kref);

/* drbd_transport.c */
int drbd_register_transport_class(struct drbd_transport_class *transport_class,
				  int version, int drbd_transport_size);
void drbd_unregister_transport_class(struct drbd_transport_class *transport_class);
struct drbd_transport_class *drbd_get_transport_class(const char *name);
void drbd_put_transport_class(struct drbd_transport_class *tc);
void drbd_print_transports_loaded(struct seq_file *seq);

int drbd_get_listener(struct drbd_path *path);
void drbd_put_listener(struct drbd_path *path);
struct drbd_path *drbd_find_path_by_addr(struct drbd_listener *listener,
					 struct sockaddr_storage *addr);
bool drbd_stream_send_timed_out(struct drbd_transport *transport,
				enum drbd_stream stream);
bool drbd_should_abort_listening(struct drbd_transport *transport);
void drbd_path_event(struct drbd_transport *transport, struct drbd_path *path);
void drbd_listener_destroy(struct kref *kref);
struct drbd_path *__drbd_next_path_ref(struct drbd_path *drbd_path,
				       struct drbd_transport *transport);

/* Might restart iteration, if current element is removed from list!! */
#define for_each_path_ref(path, transport)			\
	for (path = __drbd_next_path_ref(NULL, transport);	\
	     path;						\
	     path = __drbd_next_path_ref(path, transport))

/* drbd_receiver.c*/
struct page *drbd_alloc_pages(struct drbd_transport *transport,
			      unsigned int number, gfp_t gfp_mask);
void drbd_free_pages(struct drbd_transport *transport, struct page *page);
void drbd_control_data_ready(struct drbd_transport *transport,
			     struct drbd_const_buffer *pool);
void drbd_control_event(struct drbd_transport *transport,
			enum drbd_tr_event event);

static inline void drbd_alloc_page_chain(struct drbd_transport *t,
	struct drbd_page_chain_head *chain, unsigned int nr, gfp_t gfp_flags)
{
	chain->head = drbd_alloc_pages(t, nr, gfp_flags);
	chain->nr_pages = chain->head ? nr : 0;
}

static inline void drbd_free_page_chain(struct drbd_transport *transport,
					struct drbd_page_chain_head *chain)
{
	drbd_free_pages(transport, chain->head);
	chain->head = NULL;
	chain->nr_pages = 0;
}

/*
 * Some helper functions to deal with our page chains.
 */
/* Our transports may sometimes need to only partially use a page.
 * We need to express that somehow.  Use this struct, and "graft" it into
 * struct page at page->lru.
 *
 * According to include/linux/mm.h:
 *  | A page may be used by anyone else who does a __get_free_page().
 *  | In this case, page_count still tracks the references, and should only
 *  | be used through the normal accessor functions. The top bits of page->flags
 *  | and page->virtual store page management information, but all other fields
 *  | are unused and could be used privately, carefully. The management of this
 *  | page is the responsibility of the one who allocated it, and those who have
 *  | subsequently been given references to it.
 * (we do alloc_page(), that is equivalent).
 *
 * Red Hat struct page is different from upstream (layout and members) :(
 * So I am not too sure about the "all other fields", and it is not as easy to
 * find a place where sizeof(struct drbd_page_chain) would fit on all archs and
 * distribution-changed layouts.
 *
 * But (upstream) struct page also says:
 *  | struct list_head lru;   * ...
 *  |       * Can be used as a generic list
 *  |       * by the page owner.
 *
 * On 32bit, use unsigned short for offset and size,
 * to still fit in sizeof(page->lru).
 */

/* grafted over struct page.lru */
struct drbd_page_chain {
	struct page *next;	/* next page in chain, if any */
#ifdef CONFIG_64BIT
	unsigned int offset;	/* start offset of data within this page */
	unsigned int size;	/* number of data bytes within this page */
#else
#if PAGE_SIZE > (1U<<16)
#error "won't work."
#endif
	unsigned short offset;	/* start offset of data within this page */
	unsigned short size;	/* number of data bytes within this page */
#endif
};

static inline void dummy_for_buildbug(void)
{
	struct page *dummy;
	BUILD_BUG_ON(sizeof(struct drbd_page_chain) > sizeof(dummy->lru));
}

#define page_chain_next(page) \
	(((struct drbd_page_chain *)&(page)->lru)->next)
#define page_chain_size(page) \
	(((struct drbd_page_chain *)&(page)->lru)->size)
#define page_chain_offset(page) \
	(((struct drbd_page_chain *)&(page)->lru)->offset)
#define set_page_chain_next(page, v) \
	(((struct drbd_page_chain *)&(page)->lru)->next = (v))
#define set_page_chain_size(page, v) \
	(((struct drbd_page_chain *)&(page)->lru)->size = (v))
#define set_page_chain_offset(page, v) \
	(((struct drbd_page_chain *)&(page)->lru)->offset = (v))
#define set_page_chain_next_offset_size(page, n, o, s)		\
	(*((struct drbd_page_chain *)&(page)->lru) =		\
	((struct drbd_page_chain) {				\
		.next = (n),					\
		.offset = (o),					\
		.size = (s),					\
	 }))

#define page_chain_for_each(page) \
	for (; page && ({ prefetch(page_chain_next(page)); 1; }); \
			page = page_chain_next(page))
#define page_chain_for_each_safe(page, n) \
	for (; page && ({ n = page_chain_next(page); 1; }); page = n)

#ifndef SK_CAN_REUSE
/* This constant was introduced by Pavel Emelyanov <xemul@parallels.com> on
   Thu Apr 19 03:39:36 2012 +0000. Before the release of linux-3.5
   commit 4a17fd52 sock: Introduce named constants for sk_reuse */
#define SK_CAN_REUSE   1
#endif

#endif
