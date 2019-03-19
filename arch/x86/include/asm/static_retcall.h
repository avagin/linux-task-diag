/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2019  Dmitry Safonov, Andrey Vagin
 */

#ifndef _ASM_X86_STATIC_RETCALL_H
#define _ASM_X86_STATIC_RETCALL_H

struct retcall_entry {
	u16 code;
	u16 target;
};

static __always_inline bool timens_static_branch(void)
{
	asm_volatile_goto("1:\n\t"
		".byte " __stringify(STATIC_KEY_INIT_NOP) "\n\t"
		 ".pushsection __retcall_table,  \"aw\"\n\t"
		 "2: .word 1b - 2b, %l[l_yes] - 2b\n\t"
		 ".popsection\n\t"
		 : :  :  : l_yes);

	return false;
l_yes:
	return true;
}

#define static_retcall(func, ...)					\
	do {								\
		asm_volatile_goto(					\
			".pushsection __retcall_table, \"aw\" \n\t"	\
			"2: .word %l[l_call] - 2b\n\t"			\
			".word %l[l_return] - 2b\n\t"			\
			".word %l[l_out] - 2b\n\t"			\
			".popsection"					\
			: : : : l_call, l_return, l_out);		\
l_call:									\
		func(__VA_ARGS__);					\
l_return:								\
		return;							\
		annotate_reachable();					\
l_out:									\
		nop();							\
		return;							\
	} while(0)

#define static_retcall_int(ret, func, ...)				\
	do {								\
		asm_volatile_goto(					\
			".pushsection __retcall_table, \"aw\" \n\t"	\
			_ASM_ALIGN "\n\t"				\
			"2: .word %l[l_call] - 2b\n\t"			\
			".word %l[l_return] - 2b\n\t"			\
			".word %l[l_out] - 2b\n\t"			\
			".popsection"					\
			: : : : l_call, l_return, l_out);		\
l_call:									\
		func(__VA_ARGS__);					\
l_return:								\
		return ret;						\
		annotate_reachable();					\
l_out:									\
		nop();							\
		return ret;						\
	} while(0)

#endif
