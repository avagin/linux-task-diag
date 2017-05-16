# Makefile for mount selftests.
CFLAGS = -Wall \
         -O2

TEST_GEN_PROGS := unprivileged-remount-test

include ../lib.mk

override define RUN_TESTS
	if [ -f /proc/self/uid_map ] ; \
	then	\
		./unprivileged-remount-test ; \
	else	\
		echo "WARN: No /proc/self/uid_map exist, test skipped." ; \
	fi
	unshare -Urm ./test-reparent-mounts
	unshare -Urm ./test-reparent-mounts -c
	unshare -Urm ./test-reparent-mounts -s
	unshare -Urm ./test-reparent-mounts -s -S
endef

override EMIT_TESTS := echo "$(RUN_TESTS)"

