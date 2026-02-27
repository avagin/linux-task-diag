/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMZONE_LOCK_H
#define _LINUX_MMZONE_LOCK_H

#include <linux/mmzone.h>
#include <linux/spinlock.h>

static inline void zone_lock_init(struct zone *zone)
{
	spin_lock_init(&zone->_lock);
}

#define zone_lock_irqsave(zone, flags)				\
do {								\
	spin_lock_irqsave(&(zone)->_lock, flags);		\
} while (0)

#define zone_trylock_irqsave(zone, flags)			\
({								\
	spin_trylock_irqsave(&(zone)->_lock, flags);		\
})

static inline void zone_unlock_irqrestore(struct zone *zone, unsigned long flags)
{
	spin_unlock_irqrestore(&zone->_lock, flags);
}

static inline void zone_lock_irq(struct zone *zone)
{
	spin_lock_irq(&zone->_lock);
}

static inline void zone_unlock_irq(struct zone *zone)
{
	spin_unlock_irq(&zone->_lock);
}

#endif /* _LINUX_MMZONE_LOCK_H */
