# vramfs — GPU VRAM backed swap prototype

This repository contains a prototype that exports GPU VRAM as a block device using an nbdkit plugin and allows using it as swap. It is experimental and intended for testing and research only.

Key components
- `tools/nbd_backing/nbdkit_cuda_plugin.cpp` — nbdkit plugin exposing CUDA-allocated blocks.
- `src/cuda_memory.cpp`, `include/cuda_memory.hpp` — CUDA backend allocator + block abstraction.
- `bin/start_gpu_swap` — strict, fail-fast script: start nbdkit, attach `/dev/nbdX`, create and enable swap.
- `tools/nbd_backing/bench.sh` — strict bench: attach, run smoke I/O, create swap, log to `/tmp/vramfs_bench.log`.
- `tests/` — helper test scripts and a small C++ unit test for the memory backend.

Prerequisites
- `nbdkit`, `qemu-nbd`, `make`, `g++`, and `sudo` must be available.
- CUDA toolkit / driver and `libcudart` are required to enable the CUDA backend (`USE_CUDA=1`). Without CUDA the plugin will build but behave in a disabled/fallback mode.
- `fio` is optional — if present, scripts will run fio workloads; otherwise they use `dd` loops.

Quick start
1. Build the plugin:

   make bin/nbdkit_cuda_plugin.so USE_CUDA=1

2. Start the helper script to create and enable swap (runs strict checks and logs):

   ./bin/start_gpu_swap

3. Or run the bench (strict, fail-fast) which runs smoke I/O then enables swap:

   ./tools/nbd_backing/bench.sh

Tests
- Requirements check: `tests/requirements.sh`
- Code smoke + attach: `tests/code_tests.sh`
- Integration (attach + dd workload): `tests/integration.sh`
- Parent runner: `tests/run_all_tests.sh`
- C++ unit test (non-CUDA or CUDA builds): `make test` builds and runs `bin/test_cuda`.

Logs
- bench and plugin logs are written to `/tmp/vramfs_bench.log`, `/tmp/vramfs_ext_bench.log`, and other `/tmp/vramfs_*.log` files.

Notes and safety
- This is a prototype. Using GPU VRAM for swap can cause data loss on process crash or device reset.
- Do not use this on production systems. Test on disposable VMs or systems where a GPU reset is acceptable.

If you want: I can add GoogleTest-based unit tests, CI targets, or a `make ci` wrapper next.
