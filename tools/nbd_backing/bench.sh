#!/usr/bin/env bash
# Simple integration bench for vram-cuda nbdkit plugin
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PLUGIN=$ROOT/bin/nbdkit_cuda_plugin.so
PORT=10809
NBD_DEV=/dev/nbd1
LOG=/tmp/vramfs_bench.log

cleanup(){
  set +e
  sudo swapoff /dev/nbd0 2>/dev/null || true
  sudo qemu-nbd -d $NBD_DEV 2>/dev/null || true
  pkill -f nbdkit || true
}
trap cleanup EXIT

# Prefer using start script for consistency
if [ ! -f "$PLUGIN" ]; then
  echo "plugin not built; run: make bin/nbdkit_cuda_plugin.so USE_CUDA=1"
  exit 2
fi

echo "Starting nbdkit and attaching device via bin/start_gpu_swap"
bin/start_gpu_swap || true

# ensure log file exists and is writable
mkdir -p "$(dirname "$LOG")"
if ! touch "$LOG" 2>/dev/null; then
  sudo touch "$LOG"
fi
if ! chmod a+rw "$LOG" 2>/dev/null; then
  sudo chmod a+rw "$LOG"
fi

nbdkit -v -p $PORT $PLUGIN >>"$LOG" 2>&1 &

# wait for nbdkit to start listening on the port
for i in {1..50}; do
  if ss -ltn | grep -q ":$PORT"; then
    break
  fi
  sleep 0.1
done

if ! ss -ltn | grep -q ":$PORT"; then
  echo "nbdkit did not start (see $LOG)"
  tail -n 200 "$LOG" || true
  exit 1
fi

# If the device is already present and sized, skip explicit connect
if [ -b "$NBD_DEV" ] && [ "$(sudo blockdev --getsize64 $NBD_DEV 2>/dev/null || echo 0)" -gt 65536 ]; then
  echo "$NBD_DEV already present and sized; skipping connect"
else
  echo "Connecting $NBD_DEV"
  sudo modprobe nbd max_part=8
  sudo qemu-nbd --connect=$NBD_DEV nbd://127.0.0.1:$PORT || true

  # wait for device node to appear
  for i in {1..50}; do
    if [ -b "$NBD_DEV" ]; then
      break
    fi
    sleep 0.1
  done
  if [ ! -b "$NBD_DEV" ]; then
    echo "$NBD_DEV not present after qemu-nbd connect; see $LOG"
    tail -n 200 "$LOG" || true
    exit 1
  fi
fi

# wait for device to report non-trivial size
for i in {1..50}; do
  if [ "$(sudo blockdev --getsize64 $NBD_DEV 2>/dev/null || echo 0)" -gt 65536 ]; then
    break
  fi
  sleep 0.1
done
if [ "$(sudo blockdev --getsize64 $NBD_DEV 2>/dev/null || echo 0)" -le 65536 ]; then
  echo "$NBD_DEV has invalid size; aborting"; tail -n 200 "$LOG" || true; exit 1
fi

echo "Running smoke I/O test"
# If the device is currently used as swap, avoid writing to it; perform a read-only smoke test instead.
if swapon --show=NAME | grep -q "^$NBD_DEV$"; then
  echo "$NBD_DEV is active swap; running read-only smoke test (retries)"
  TMP_DUMP=/tmp/nbd_smoke.dump
  rm -f "$TMP_DUMP"
  success=0
  for i in 1 2 3; do
    sudo dd if=$NBD_DEV of="$TMP_DUMP" bs=64k count=16 iflag=direct >/dev/null 2>&1 || true
    if [ -s "$TMP_DUMP" ]; then
      success=1
      break
    fi
    echo "read attempt $i failed; retrying..."
    sleep 0.2
  done
  if [ $success -eq 1 ]; then
    ls -l "$TMP_DUMP"
  else
    echo "read smoke failed after retries"
  fi
else
  if command -v fio >/dev/null 2>&1; then
    fio --name=write_test --filename=$NBD_DEV --rw=randwrite --bs=4k --size=16M --numjobs=2 --runtime=5 --time_based --iodepth=8 --direct=1 || true
    fio --name=read_test --filename=$NBD_DEV --rw=randread --bs=4k --size=16M --numjobs=2 --runtime=5 --time_based --iodepth=8 --direct=1 || true
  else
    sudo dd if=/dev/urandom of=$NBD_DEV bs=64k count=16 oflag=direct || true
    sudo dd if=$NBD_DEV of=/tmp/nbd_smoke.dump bs=64k count=16 iflag=direct || true
    ls -l /tmp/nbd_smoke.dump || true
  fi
fi

echo "Done; logs: $LOG"
