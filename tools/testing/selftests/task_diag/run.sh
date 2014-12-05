unshare -p -f -m --mount-proc ./_run.sh && { echo PASS; exit 0; } || { echo FAIL; exit 1; }
