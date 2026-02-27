/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM zone_lock

#if !defined(_TRACE_ZONE_LOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ZONE_LOCK_H

#include <linux/tracepoint.h>
#include <linux/types.h>

struct zone;

DECLARE_EVENT_CLASS(zone_lock,

	TP_PROTO(struct zone *zone),

	TP_ARGS(zone),

	TP_STRUCT__entry(
		__field(struct zone *, zone)
	),

	TP_fast_assign(
		__entry->zone = zone;
	),

	TP_printk("zone=%p", __entry->zone)
);

#define DEFINE_ZONE_LOCK_EVENT(name)			\
	DEFINE_EVENT(zone_lock, name,			\
		TP_PROTO(struct zone *zone),		\
		TP_ARGS(zone))

DEFINE_ZONE_LOCK_EVENT(zone_lock_start_locking);
DEFINE_ZONE_LOCK_EVENT(zone_lock_released);

TRACE_EVENT(zone_lock_acquire_returned,

	TP_PROTO(struct zone *zone, bool success),

	TP_ARGS(zone, success),

	TP_STRUCT__entry(
		__field(struct zone *, zone)
		__field(bool, success)
	),

	TP_fast_assign(
		__entry->zone = zone;
		__entry->success = success;
	),

	TP_printk(
		"zone=%p success=%s",
		__entry->zone,
		__entry->success ? "true" : "false"
	)
);

#endif /* _TRACE_ZONE_LOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
