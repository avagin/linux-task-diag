#!/bin/sh
set -o pipefail
set -e -x

./fork 1000

nprocesses=`./task_diag_all all --maps | grep 'pid.*tgid.*ppid.*comm fork$' | wc -l`
nthreads=`./task_diag_all All --smaps --cred | grep 'pid.*tgid.*ppid.*comm fork$' | wc -l`
nchildren=`./task_diag_all children --pid 1 | grep 'pid.*tgid.*ppid.*comm fork$' | wc -l`

./task_diag_all one --pid 1 --cred

killall -9 fork

[ "$nthreads"     -eq 1000 ] &&
[ "$nprocesses"   -eq 1000  ] &&
[ "$nchildren"    -eq 1000  ] &&
true ||  {
	echo "Unexpected number of tasks $nthreads:$nprocesses" 1>&2
	exit 1
}
