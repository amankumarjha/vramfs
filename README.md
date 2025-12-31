# vramswap — GPU VRAM backed swap prototype

This repository contains a prototype that exports GPU VRAM as a block device using an nbdkit plugin and allows using it as swap. It is experimental and intended for testing and research only.

Key components
- `tools/nbd_backing/nbdkit_cuda_plugin.cpp` — nbdkit plugin exposing CUDA-allocated blocks.
- `src/cuda_memory.cpp`, `include/cuda_memory.hpp` — CUDA backend allocator + block abstraction.
- `bin/start_gpu_swap` — strict, fail-fast script: start nbdkit, attach `/dev/nbdX`, create and enable swap.
- `tools/nbd_backing/bench.sh` — strict bench: attach, run smoke I/O, create swap, log to `/tmp/vramswap_bench.log`.
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

Run notes and troubleshooting
- To run the starter with sudo (recommended) and have logs written to `/tmp/vramswap_nbdkit.log`:

```bash
sudo LOG=/tmp/vramswap_nbdkit.log ./bin/start_gpu_swap
```

- If you see a `tee: /tmp/vramswap_nbdkit.log: Permission denied` error, the script will attempt to create and chmod the logfile with `sudo` for you. You can also fix this manually before running:

```bash
sudo touch /tmp/vramswap_nbdkit.log
sudo chmod a+rw /tmp/vramswap_nbdkit.log
sudo LOG=/tmp/vramswap_nbdkit.log ./bin/start_gpu_swap
```

- If your environment restricts writing to `/tmp` (containers/sandboxes), run without a logfile and stream output to your terminal:

```bash
LOG=/dev/stdout sudo ./bin/start_gpu_swap
```

- To verify swap is enabled after the helper completes:

```bash
sudo swapon --show
```

Auto-detect and custom size
- The nbdkit CUDA plugin now supports automatic export sizing and a `size=` config option.
- By default the plugin will auto-detect the device total memory and use `device_total - 256M` as a safe default.
- To explicitly set the export size (e.g. 4G) use the plugin `size` config when starting nbdkit. Example:

```bash
# start nbdkit with explicit 4GiB export
nbdkit -f -v -p 10809 $PWD/bin/nbdkit_cuda_plugin.so size=4G

# or via the helper (plugin receives extra args forwarded by nbdkit)
sudo LOG=/dev/stdout ./bin/start_gpu_swap PLUGIN="$PWD/bin/nbdkit_cuda_plugin.so" \
   && sudo qemu-nbd --format=raw --persistent --connect=/dev/nbd1 nbd://127.0.0.1:10809
```

Notes on allocation strategy
- The CUDA backend now allocates device memory in larger 4MiB chunks and slices them into 64KiB blocks to avoid many small `cudaMalloc` calls.
- Allocating several GiB may still fail or interfere with display drivers — proceed carefully and ensure the device has capacity.

3. Or run the bench (strict, fail-fast) which runs smoke I/O then enables swap:

   ./tools/nbd_backing/bench.sh

Tests
- Requirements check: `tests/requirements.sh`
- Code smoke + attach: `tests/code_tests.sh`
- Integration (attach + dd workload): `tests/integration.sh`
- Parent runner: `tests/run_all_tests.sh`
- C++ unit test (non-CUDA or CUDA builds): `make test` builds and runs `bin/test_cuda`.

Logs
- bench and plugin logs are written to `/tmp/vramswap_bench.log`, `/tmp/vramswap_ext_bench.log`, and other `/tmp/vramswap_*.log` files.

Notes and safety
- This is a prototype. Using GPU VRAM for swap can cause data loss on process crash or device reset.
- Do not use this on production systems. Test on disposable VMs or systems where a GPU reset is acceptable.

If you want: I can add GoogleTest-based unit tests, CI targets, or a `make ci` wrapper next.
