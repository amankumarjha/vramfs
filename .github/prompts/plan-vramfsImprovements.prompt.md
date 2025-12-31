Plan: NBD + CUDA prototype (no OpenCL)

TL;DR — Focus this effort solely on building an NBD-backed user-space prototype that uses CUDA to allocate and manage GPU memory. The prototype should expose GPU-backed blocks to the kernel via NBD so you can safely test using the GPU as secondary swap; do not use FUSE for swap.

Steps
1. Confirm CUDA & NBD environment
   - Verify CUDA drivers/toolkit (`nvidia-smi`, `nvcc`), kernel NBD support, and required dev packages.

2. Implement CUDA memory backend
   - Add a `cuda_memory` backend to allocate VRAM, manage pinned host staging buffers, and provide async read/write via CUDA streams. Key files: `include/cuda_memory.hpp`, `src/cuda_memory.cpp`.

3. Prototype NBD server exposing GPU blocks
   - Implement a user-space NBD server or `nbdkit` plugin that maps NBD read/write requests to the CUDA-backed blocks. Key file: `tools/nbd_backing/nbd_server.cpp`.

4. Block layout and performance tuning
   - Use 4KiB pages for compatibility but batch internally into larger transfers (configurable, e.g. 64KiB–128KiB) to improve throughput. Implement internal batching, pinned staging pool reuse, and multiple CUDA streams.

5. Safety, deployment and monitoring
   - Run the NBD server as a supervised `systemd` service with restart and watchdog. Use low swap priority when calling `swapon`. Implement crash recovery and optional local fast fallback (zram) so critical pages remain safe.

6. Benchmarking and validation
   - Add scripts using `dd` and `fio` to measure random/sustained throughput and latency. Tune batching and stream counts accordingly.

7. Production path (optional)
   - If prototype proves stable and latency is acceptable, plan a kernel block-driver or kernel+user hybrid for tighter integration and safer failure modes.

Key decisions & caveats
- Do not use a FUSE filesystem for swap — it is unsafe. Use NBD or a kernel block device.  
- CUDA is recommended for NVIDIA GPUs: pinned host buffers, CUDA streams, and `cudaMemcpyAsync` give better performance and control than OpenCL on NVIDIA.  
- Preallocate VRAM and pinned host staging buffers; avoid overcommitting VRAM to prevent driver/system instability.  
- Expect higher random-swap latency than host RAM; use GPU swap as secondary/low-priority swap only.

Files to add/change
- Add: `include/cuda_memory.hpp`, `src/cuda_memory.cpp`, `tools/nbd_backing/nbd_server.cpp`.  
- Modify: `Makefile` to optionally build the NBD server and add CUDA flags.  
- Update docs: `README.md` with usage, `systemd` unit example, and safety guidance.

Next step
- I can scaffold the minimal NBD + CUDA prototype (headers, basic server loop, systemd unit and build changes). Shall I scaffold that now? 
