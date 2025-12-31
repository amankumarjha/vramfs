#!/usr/bin/env bash
set -euo pipefail

# bench.sh: strict fail-fast integration bench for GPU-backed NBD swap.
# Starts nbdkit, attaches via qemu-nbd, runs write+read smoke I/O before
# enabling swap, then creates and enables swap. All actions log timestamps
# and the script exits immediately on any error.

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PLUGIN=${PLUGIN:-$ROOT/bin/nbdkit_cuda_plugin.so}
NBD_PORT=${NBD_PORT:-10809}
NBD_DEVICE=${NBD_DEVICE:-/dev/nbd1}
LOG=${LOG:-/tmp/vramswap_bench.log}

log() { echo "$(date -Is) [bench] $*" | tee -a "$LOG"; }

cleanup() {
  set +e
  log "cleanup: detaching ${NBD_DEVICE} and turning off swap if present"
  sudo swapoff ${NBD_DEVICE} 2>/dev/null || true
  sudo qemu-nbd -d ${NBD_DEVICE} 2>/dev/null || true
}
trap cleanup EXIT

[ -f "$PLUGIN" ] || { echo "$(date -Is) [bench] ERROR: plugin not found at $PLUGIN" | tee -a "$LOG"; exit 2; }

log "Starting nbdkit with plugin $PLUGIN on port $NBD_PORT"
nohup nbdkit -f -v -p ${NBD_PORT} "$PLUGIN" >>"$LOG" 2>&1 &

log "Waiting for nbdkit to listen on ${NBD_PORT}"
for i in {1..50}; do
  ss -ltn | grep -q ":${NBD_PORT}" && break
  sleep 0.1
done
ss -ltn | grep -q ":${NBD_PORT}" || { log "nbdkit did not start"; exit 1; }

log "Attaching qemu-nbd to ${NBD_DEVICE}"
sudo modprobe nbd max_part=8
sudo qemu-nbd --format=raw --persistent --connect=${NBD_DEVICE} nbd://127.0.0.1:${NBD_PORT} >>"$LOG" 2>&1

log "Waiting for device ${NBD_DEVICE} and valid size"
for i in {1..50}; do
  [ -b "$NBD_DEVICE" ] || { sleep 0.1; continue; }
  size=$(sudo blockdev --getsize64 "$NBD_DEVICE" 2>/dev/null || echo 0)
  [ "$size" -gt 65536 ] && break
  sleep 0.1
done
size=$(sudo blockdev --getsize64 "$NBD_DEVICE" 2>/dev/null || echo 0)
[ "$size" -gt 65536 ] || { log "device $NBD_DEVICE has invalid size ($size)"; exit 1; }

log "Running write smoke I/O against ${NBD_DEVICE} (before enabling swap)"
  if command -v fio >/dev/null 2>&1; then
    sudo fio --name=write_test --rw=write --bs=64k --size=256k --filename=${NBD_DEVICE} --output-format=normal | tee -a "$LOG"
    sudo fio --name=read_test --rw=read --bs=64k --size=256k --filename=${NBD_DEVICE} --output-format=normal | tee -a "$LOG"
  else
    log "fio not found; using dd"
    sudo dd if=/dev/zero of=${NBD_DEVICE} bs=64K count=4 conv=fdatasync 2>&1 | tee -a "$LOG"
    tmp=$(mktemp)
    sudo dd if=${NBD_DEVICE} of=- bs=64K count=4 2>/dev/null | tee ${tmp} >/dev/null
    ls -l ${tmp} | tee -a "$LOG"
    rm -f ${tmp}
  fi

log "Creating swap area and enabling swap on ${NBD_DEVICE}"
sudo mkswap -f ${NBD_DEVICE} | tee -a "$LOG"
sudo swapon -p 1 ${NBD_DEVICE} | tee -a "$LOG"

log "Bench completed. Current swap status:"
sudo swapon --show | tee -a "$LOG"

log "bench.sh finished successfully"
exit 0
