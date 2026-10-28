#!/bin/sh
set -e
echo "=== Testing RingGuard Loader ==="
if [ -x /mnt/loader ]; then
    timeout 5 /mnt/loader --allow-all &
    PID=$!
    sleep 2
    kill $PID 2>/dev/null || true
    wait $PID 2>/dev/null || true
    echo "LOADER_DONE"
else
    echo "NO_LOADER"
fi
