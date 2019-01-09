TEST_GEN_PROGS := timens timerfd timer clock_nanosleep procfs gettime_perf

uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/)
ifeq ($(ARCH),x86_64)
TEST_GEN_PROGS += gettime_perf_cold
endif

CFLAGS := -Wall -Werror
LDFLAGS := -lrt -ldl

include ../lib.mk
