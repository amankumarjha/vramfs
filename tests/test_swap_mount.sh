#!/usr/bin/env bash
set -euo pipefail

# Lightweight test: start helper, verify /dev/nbd1 is active as swap, then cleanup
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOG=${LOG:-/tmp/vramswap_test_swap_mount.log}
PLUGIN=${PLUGIN:-$ROOT/bin/nbdkit_cuda_plugin.so}
NBD_DEVICE=${NBD_DEVICE:-/dev/nbd1}

echo "$(date -Is) [test-swap] starting" | tee -a "$LOG"

[ -f "$PLUGIN" ] || { echo "ERROR: plugin not found at $PLUGIN" | tee -a "$LOG"; exit 2; }

echo "$(date -Is) [test-swap] running start_gpu_swap (small size)" | tee -a "$LOG"
# Ensure the logfile is writable by this user so we don't need sudo to tee into it.
mkdir -p "$(dirname "$LOG")"
touch "$LOG" && chmod a+rw "$LOG" || true
# Invoke helper without sudo where possible; helper will use sudo for privileged steps.
LOG=$LOG PLUGIN="$PLUGIN" PLUGIN_ARGS="size=256M" $ROOT/bin/start_gpu_swap | tee -a "$LOG"

echo "$(date -Is) [test-swap] verifying swap is active" | tee -a "$LOG"
sudo swapon --show | tee -a "$LOG"
sudo swapon --show --noheadings --raw | awk '{print $1}' | grep -qx "$NBD_DEVICE" || { echo "ERROR: $NBD_DEVICE not active as swap" | tee -a "$LOG"; exit 1; }

# Verify GPU swap priority is higher than any other active swap
gpu_prio=$(sudo swapon --show=NAME,PRIO --noheadings | awk -v d="$NBD_DEVICE" '$1==d {print $2}')
if [ -z "$gpu_prio" ]; then
	echo "ERROR: could not determine GPU swap priority" | tee -a "$LOG"; exit 1
fi
max_other=$(sudo swapon --show=NAME,PRIO --noheadings | awk -v d="$NBD_DEVICE" '$1!=d {print $2}' | sort -n | tail -n1 || echo "-32768")
if [ -n "$max_other" ] && [ "$gpu_prio" -le "$max_other" ]; then
	echo "ERROR: GPU swap priority ($gpu_prio) is not greater than other swap priority ($max_other)" | tee -a "$LOG"; exit 1
fi

echo "$(date -Is) [test-swap] cleaning up" | tee -a "$LOG"
sudo swapoff "$NBD_DEVICE" || true
sudo qemu-nbd -d "$NBD_DEVICE" || true
pkill -f "nbdkit .*nbdkit_cuda_plugin.so" || true

echo "$(date -Is) [test-swap] done" | tee -a "$LOG"
exit 0
