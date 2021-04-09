# SPDX-License-Identifier: GPL-2.0

UNAME_M := $(shell uname -m)
TEST_GEN_PROGS_x86_64 := process_vm_exec process_vm_exec_fault ptrace_vm_exec process_vm_exec_syscall
TEST_GEN_PROGS += $(TEST_GEN_PROGS_$(UNAME_M))

include ../lib.mk
