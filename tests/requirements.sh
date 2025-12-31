#!/usr/bin/env bash
set -euo pipefail

# requirements.sh - verify environment for vramfs NBD CUDA plugin
LOG=${LOG:-/tmp/vramfs_requirements.log}
echo "$(date -Is) [req] starting" | tee -a "$LOG"

need() { command -v "$1" >/dev/null 2>&1 || { echo "$(date -Is) [req] MISSING: $1" | tee -a "$LOG"; exit 2; } }

need nbdkit
need qemu-nbd
need make
need g++
need sudo

# Optional but recommended
if command -v nvidia-smi >/dev/null 2>&1; then
  echo "$(date -Is) [req] GPU: nvidia-smi present" | tee -a "$LOG"
else
  if lspci | grep -i nvidia >/dev/null 2>&1; then
    echo "$(date -Is) [req] GPU detected via lspci" | tee -a "$LOG"
  else
    echo "$(date -Is) [req] WARNING: no NVIDIA GPU detected (nvidia-smi/lspci)" | tee -a "$LOG"
  fi
fi

echo "$(date -Is) [req] done" | tee -a "$LOG"
exit 0
