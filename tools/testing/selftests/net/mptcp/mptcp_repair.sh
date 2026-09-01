#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source "$(dirname "$0")/mptcp_lib.sh"

ret=0
sin=""

cleanup()
{
	mptcp_lib_check_mptcp
	mptcp_lib_cleanup
}

trap cleanup EXIT

mptcp_lib_check_mptcp

./mptcp_repair
if [ $? -ne 0 ]; then
	echo "FAIL: mptcp_repair failed"
	ret=1
else
	echo "PASS: mptcp_repair succeeded"
fi

exit $ret
