# vramswap — quick commands

- Build plugin: compile nbdkit CUDA plugin

  make bin/nbdkit_cuda_plugin.so USE_CUDA=1

- Run tests: build, attach, smoke I/O and swap check

  ./tests/run_all_tests.sh
  make test

- Start GPU-backed swap (auto-size, logs)

  LOG=/tmp/vramswap_nbdkit.log PLUGIN="$PWD/bin/nbdkit_cuda_plugin.so" ./bin/start_gpu_swap

- Start with explicit size and higher priority (GPU swap preferred)

  SWAP_PRIO=200 PLUGIN_ARGS="size=4G" LOG=/tmp/vramswap_nbdkit.log ./bin/start_gpu_swap

- Stop/cleanup: disable and detach GPU swap

  sudo swapoff /dev/nbd1 && sudo qemu-nbd --disconnect /dev/nbd1 && sudo pkill -f 'nbdkit .*nbdkit_cuda_plugin.so'
