#!/usr/bin/env bash
set -euo pipefail

# code_tests.sh - build plugin and run a small smoke test (no swap enablement)
LOG=${LOG:-/tmp/vramswap_code_tests.log}
echo "$(date -Is) [code] starting" | tee -a "$LOG"

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

echo "$(date -Is) [code] Building plugin" | tee -a "$LOG"
make bin/nbdkit_cuda_plugin.so USE_CUDA=1 >>"$LOG" 2>&1

PLUGIN=$ROOT/bin/nbdkit_cuda_plugin.so
PORT=10809
NBD_DEVICE=/dev/nbd1

[ -f "$PLUGIN" ] || { echo "$(date -Is) [code] ERROR: plugin not found" | tee -a "$LOG"; exit 2; }

echo "$(date -Is) [code] Starting nbdkit" | tee -a "$LOG"
nohup nbdkit -f -v -p ${PORT} "$PLUGIN" >>"$LOG" 2>&1 &
sleep 0.2

echo "$(date -Is) [code] Attaching ${NBD_DEVICE}" | tee -a "$LOG"
sudo modprobe nbd max_part=8
sudo qemu-nbd --format=raw --persistent --connect=${NBD_DEVICE} nbd://127.0.0.1:${PORT} >>"$LOG" 2>&1

for i in {1..50}; do
  [ -b "$NBD_DEVICE" ] || { sleep 0.1; continue; }
  size=$(sudo blockdev --getsize64 "$NBD_DEVICE" 2>/dev/null || echo 0)
  [ "$size" -gt 65536 ] && break
  sleep 0.1
done

echo "$(date -Is) [code] Running small I/O check" | tee -a "$LOG"
sudo dd if=/dev/zero of=${NBD_DEVICE} bs=64K count=2 conv=fdatasync 2>&1 | tee -a "$LOG"
sudo dd if=${NBD_DEVICE} of=/tmp/vramswap_code_test.dump bs=64K count=2 2>&1 | tee -a "$LOG"

echo "$(date -Is) [code] Cleaning up" | tee -a "$LOG"
sudo qemu-nbd -d ${NBD_DEVICE} >>"$LOG" 2>&1 || true
pkill -f "nbdkit .*nbdkit_cuda_plugin.so" || true

echo "$(date -Is) [code] done" | tee -a "$LOG"
exit 0
