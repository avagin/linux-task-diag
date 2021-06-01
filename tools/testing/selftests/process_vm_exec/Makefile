# SPDX-License-Identifier: GPL-2.0

UNAME_M := $(shell uname -m)
ARCH ?= $(shell echo $(UNAME_M) | sed -e s/i.86/x86/ -e s/x86_64/x86/ -e s/aarch64/arm64/)

CFLAGS += -iquote../../../../include/uapi -iquote../../../../arch/$(ARCH)/include/uapi

TEST_GEN_PROGS_x86_64 := process_vm_exec process_vm_exec_fault ptrace_vm_exec process_vm_exec_syscall process_vm_exec_signal
TEST_GEN_PROGS_aarch64 := $(TEST_GEN_PROGS_x86_64)
TEST_GEN_PROGS += $(TEST_GEN_PROGS_$(UNAME_M))

include ../lib.mk

$(OUTPUT)/process_vm_exec_signal: CFLAGS += -DPROCESS_VM_EXEC_TEST_SIGNAL

