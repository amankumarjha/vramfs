#!/bin/bash
# Simple wrapper for nbdkit sh plugin used for discovery and debugging.
LOG=/tmp/nbdkit_sh_wrapper.log
# Redirect all stdout/stderr to the log, keep fd3 as the real stdout for replies
exec 3>&1 4>&2
exec 1>>"$LOG" 2>&1
echo "---- invocation ----"
echo "ARGS: $@"
echo "ENV:"
env

# Try to detect method name from common env vars
METHOD=${NBDKIT_METHOD:-${METHOD:-${nbdkit_method:-}}}

if [ -z "$METHOD" ]; then
  # Try first arg
  METHOD=$1
fi

case "$METHOD" in
  get_size)
    # Return a small size (e.g., 64MiB) to the real stdout (fd 3)
    echo $((64 * 1024 * 1024)) >&3
    ;;
  can_write|can_zero|can_fast_zero|can_trim|can_fua)
    # nbdkit/sh plugin expects capability via exit code: 0 = supported, 1 = not
    case "$METHOD" in
      can_write|can_fua)
        exit 0
        ;;
      can_zero|can_fast_zero|can_trim)
        exit 1
        ;;
    esac
    ;;
  pread)
    # plugin should request offset and count in env variables; log and return zeros
    echo "pread called"
    COUNT=${NBDKIT_COUNT:-${COUNT:-${nbdkit_count:-}}}
    # default: output COUNT zero bytes
    if [ -z "$COUNT" ]; then
      COUNT=4096
    fi
    dd if=/dev/zero bs=1 count="$COUNT" 2>/dev/null >&3
    ;;
  pwrite)
    echo "pwrite called"
    # read data from stdin and discard
    cat >/dev/null
    ;;
  # Lifecycle and auxiliary methods: log and succeed
  load|magic_config_key|config_complete|thread_model|get_ready|preconnect|open|close|default_export)
    # no-op but log invocation (already logged at top)
    exit 0
    ;;
  *)
    # default: just exit
    exit 0
    ;;
esac
