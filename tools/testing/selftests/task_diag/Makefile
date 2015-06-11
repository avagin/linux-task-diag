all: task_diag_all fork task_proc_all fork

CFLAGS += -g -Wall -O2 -I/usr/include/libnl3
LDFLAGS += -lnl-3
TEST_PROGS := run.sh
include ../lib.mk

task_diag_all.o: task_diag_all.c task_diag_comm.h
task_diag_comm.o: task_diag_comm.c task_diag_comm.h

task_diag_all: task_diag_all.o task_diag_comm.o
fork: fork.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lpthread

task_proc_all: task_proc_all.c

clean:
	rm -rf task_diag task_diag_all task_diag_comm.o task_diag_all.o task_diag.o fork task_proc_all
