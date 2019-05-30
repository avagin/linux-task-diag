/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSHYPER_TSCPAGE_H
#define _ASM_X86_MSHYPER_TSCPAGE_H

#include <asm/hyperv-tlfs.h>

#ifdef CONFIG_HYPERV_TSCPAGE
struct ms_hyperv_tsc_page *hv_get_tsc_page(void);
static inline u64 hv_read_tsc_page_tsc(const struct ms_hyperv_tsc_page *tsc_pg,
				       u64 *cur_tsc)
{
	u64 scale, offset;
	u32 sequence;

	/*
	 * The protocol for reading Hyper-V TSC page is specified in Hypervisor
	 * Top-Level Functional Specification ver. 3.0 and above. To get the
	 * reference time we must do the following:
	 * - READ ReferenceTscSequence
	 *   A special '0' value indicates the time source is unreliable and we
	 *   need to use something else. The currently published specification
	 *   versions (up to 4.0b) contain a mistake and wrongly claim '-1'
	 *   instead of '0' as the special value, see commit c35b82ef0294.
	 * - ReferenceTime =
	 *        ((RDTSC() * ReferenceTscScale) >> 64) + ReferenceTscOffset
	 * - READ ReferenceTscSequence again. In case its value has changed
	 *   since our first reading we need to discard ReferenceTime and repeat
	 *   the whole sequence as the hypervisor was updating the page in
	 *   between.
	 */
	do {
		sequence = READ_ONCE(tsc_pg->tsc_sequence);
		if (!sequence)
			return U64_MAX;
		/*
		 * Make sure we read sequence before we read other values from
		 * TSC page.
		 */
		smp_rmb();

		scale = READ_ONCE(tsc_pg->tsc_scale);
		offset = READ_ONCE(tsc_pg->tsc_offset);
		*cur_tsc = rdtsc_ordered();

		/*
		 * Make sure we read sequence after we read all other values
		 * from TSC page.
		 */
		smp_rmb();

	} while (READ_ONCE(tsc_pg->tsc_sequence) != sequence);

	return mul_u64_u64_shr(*cur_tsc, scale, 64) + offset;
}

static inline u64 hv_read_tsc_page(const struct ms_hyperv_tsc_page *tsc_pg)
{
	u64 cur_tsc;

	return hv_read_tsc_page_tsc(tsc_pg, &cur_tsc);
}

#else
static inline struct ms_hyperv_tsc_page *hv_get_tsc_page(void)
{
	return NULL;
}

static inline u64 hv_read_tsc_page_tsc(const struct ms_hyperv_tsc_page *tsc_pg,
				       u64 *cur_tsc)
{
	BUG();
	return U64_MAX;
}
#endif
#endif
