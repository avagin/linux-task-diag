/*
 * Adapted from arm64 version.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_VDSO_DATAPAGE_H
#define __ASM_VDSO_DATAPAGE_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>
#include <asm/page.h>

union vdso_data_store {
	struct vdso_data	data[CS_BASES];
	u8			page[PAGE_SIZE];
};

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_DATAPAGE_H */
