#!/usr/bin/env bash
set -euo pipefail

# integration.sh - attach NBD device, run I/O tests, and detach (no swapon)
LOG=${LOG:-/tmp/vramswap_integration.log}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLUGIN=${PLUGIN:-$ROOT/bin/nbdkit_cuda_plugin.so}
PORT=${PORT:-10809}
NBD_DEVICE=${NBD_DEVICE:-/dev/nbd1}

echo "$(date -Is) [int] Starting nbdkit" | tee -a "$LOG"
nohup nbdkit -f -v -p ${PORT} "$PLUGIN" >>"$LOG" 2>&1 &
sleep 0.2

echo "$(date -Is) [int] Attaching ${NBD_DEVICE}" | tee -a "$LOG"
sudo modprobe nbd max_part=8
sudo qemu-nbd --format=raw --persistent --connect=${NBD_DEVICE} nbd://127.0.0.1:${PORT} >>"$LOG" 2>&1

for i in {1..50}; do
  [ -b "$NBD_DEVICE" ] || { sleep 0.1; continue; }
  size=$(sudo blockdev --getsize64 "$NBD_DEVICE" 2>/dev/null || echo 0)
  [ "$size" -gt 65536 ] && break
  sleep 0.1
done

echo "$(date -Is) [int] Running 15s read/write workload (dd loop)" | tee -a "$LOG"
end=$((SECONDS+15))
while [ $SECONDS -lt $end ]; do
  sudo dd if=/dev/zero of=${NBD_DEVICE} bs=64K count=4 conv=fdatasync >/dev/null 2>&1
  sudo dd if=${NBD_DEVICE} of=/dev/null bs=64K count=4 >/dev/null 2>&1
done

echo "$(date -Is) [int] Detaching and cleaning up" | tee -a "$LOG"
sudo qemu-nbd -d ${NBD_DEVICE} >>"$LOG" 2>&1 || true
pkill -f "nbdkit .*nbdkit_cuda_plugin.so" || true

echo "$(date -Is) [int] done" | tee -a "$LOG"
exit 0
