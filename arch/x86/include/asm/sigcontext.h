/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SIGCONTEXT_H
#define _ASM_X86_SIGCONTEXT_H

/* This is a legacy header - all kernel code includes <uapi/asm/sigcontext.h> directly. */

#include <uapi/asm/sigcontext.h>

extern long swap_vm_exec_context(struct sigcontext __user *uctx);

#endif /* _ASM_X86_SIGCONTEXT_H */
