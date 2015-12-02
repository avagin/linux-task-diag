#!/bin/sh
set -o pipefail
set -e -x

./fork 1000 10

nprocesses=`./task_diag_all all --maps | grep 'pid.*tgid.*ppid.*comm fork$' | wc -l`
nthreads=`./task_diag_all All --smaps --cred | grep 'pid.*tgid.*ppid.*comm fork$' | wc -l`
nchildren=`./task_diag_all children --pid 1 | grep 'pid.*tgid.*ppid.*comm fork$' | wc -l`

./task_diag_all one --pid 1 --cred

( exec -a fork_thread ./fork 1 1234 )
pid=`pidof fork_thread`
ntaskthreads=`./task_diag_all thread --maps --cred --smaps --pid $pid |  grep 'pid.*tgid.*ppid.*comm' | wc -l`
killall -9 fork

[ "$nthreads"     -eq 10000 ] &&
[ "$nprocesses"   -eq 1000  ] &&
[ "$nchildren"    -eq 1000  ] &&
[ "$ntaskthreads" -eq 1234  ] &&
true ||  {
	echo "Unexpected number of tasks $nthreads:$nprocesses" 1>&2
	exit 1
}
