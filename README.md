# vramswap

Prototype VRAM-backed swap for Linux and it is tested on NVIDIA RTX 3050 in Ubuntu Gnome

This repo builds an `nbdkit` plugin that exposes GPU VRAM (via CUDA) as a raw block device, and a helper script that attaches it via `qemu-nbd` and enables swap on `/dev/nbd*`.

## Repo layout

- `tools/nbd_backing/nbdkit_cuda_plugin.cpp`: `nbdkit` plugin entrypoints (pread/pwrite/get_size)
- `src/cuda_memory.cpp`, `include/cuda_memory.hpp`: CUDA memory pool/block backend
- `bin/start_gpu_swap`: helper to start `nbdkit`, connect `qemu-nbd`, run `mkswap`, and `swapon`
- `tests/`: requirements check + smoke/integration tests

## Requirements

- Linux with `sudo` access
- `nbd` kernel module (`sudo modprobe nbd max_part=8`)
- Runtime tools: `nbdkit`, `qemu-nbd`
- Build tools: `g++` (C++20), `make`
- CUDA: NVIDIA driver + CUDA runtime/SDK (build uses `-lcudart` when `USE_CUDA=1`)

Notes:

- The current `Makefile` always pulls `fuse3` and `OpenCL` flags via `pkg-config`/`-lOpenCL`, even when you only build the `nbdkit` plugin.
- You may need the development packages for `nbdkit`, `fuse3`, and OpenCL installed for compilation to succeed.

## Build

Build the `nbdkit` CUDA plugin:

```bash
make bin/nbdkit_cuda_plugin.so USE_CUDA=1
```

Optional: build and run the small CUDA backend unit test:

```bash
make test USE_CUDA=1
```

## Start GPU-backed swap

Auto-size (uses roughly “total VRAM minus a safety reserve”) and log to a file:

```bash
LOG=/tmp/vramswap_nbdkit.log \
  PLUGIN="$PWD/bin/nbdkit_cuda_plugin.so" \
  ./bin/start_gpu_swap
```

Explicit size and higher swap priority (prefer GPU swap over other swap devices):

```bash
SWAP_PRIO=200 \
  PLUGIN_ARGS="size=4G" \
  LOG=/tmp/vramswap_nbdkit.log \
  ./bin/start_gpu_swap
```

Check swap status:

```bash
sudo swapon --show
```

## Stop / cleanup

```bash
sudo swapoff /dev/nbd1
sudo qemu-nbd --disconnect /dev/nbd1
sudo pkill -f 'nbdkit .*nbdkit_cuda_plugin.so' || true
```

## Configuration

### `bin/start_gpu_swap` environment variables

- `PORT` (default `10809`): TCP port for `nbdkit`
- `NBD_DEVICE` (default `/dev/nbd1`): device to attach via `qemu-nbd`
- `PLUGIN` (default `$PWD/bin/nbdkit_cuda_plugin.so`): path to the plugin `.so`
- `LOG` (default `/tmp/vramswap_nbdkit.log`): log file
- `SWAP_PRIO` (default `100`): swap priority passed to `swapon -p`
- `PLUGIN_ARGS`: extra plugin args forwarded to `nbdkit` (example: `size=4G`)

### `nbdkit` plugin args

- `size=<bytes|K|M|G>`: exported device size (example: `size=256M`, `size=4G`)
- If `size` is omitted, the plugin auto-detects the current device’s total VRAM and uses “total - 256MiB” as a safety reserve.

## Tests

Run all checks (requirements + build + attach/I/O + swap enable test):

```bash
./tests/run_all_tests.sh
```

Individual scripts:

- `./tests/requirements.sh`
- `./tests/code_tests.sh`
- `./tests/integration.sh`
- `./tests/test_swap_mount.sh`

Logs are written under `/tmp/` by default (see `LOG=...`).
