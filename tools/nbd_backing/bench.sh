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

if [ ! -f "$PLUGIN" ]; then
  echo "plugin not built; run: make bin/nbdkit_cuda_plugin.so USE_CUDA=1"
  exit 2
fi

echo "Starting nbdkit with $PLUGIN"
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

echo "Connecting $NBD_DEV"
sudo modprobe nbd max_part=8
sudo qemu-nbd --connect=$NBD_DEV nbd://127.0.0.1:$PORT

if command -v fio >/dev/null 2>&1; then
  echo "Running fio random write/read test"
  fio --name=write_test --filename=$NBD_DEV --rw=randwrite --bs=4k --size=16M --numjobs=4 --time_based --runtime=10 --iodepth=16 --direct=1
  fio --name=read_test --filename=$NBD_DEV --rw=randread --bs=4k --size=16M --numjobs=4 --time_based --runtime=10 --iodepth=16 --direct=1
else
  echo "fio not installed; running simple dd smoke test"
  sudo dd if=/dev/urandom of=$NBD_DEV bs=4k count=16 oflag=direct || true
  sudo dd if=$NBD_DEV of=/tmp/nbd_smoke.dump bs=4k count=16 iflag=direct || true
  ls -l /tmp/nbd_smoke.dump || true
fi

echo "Done; logs: $LOG"
