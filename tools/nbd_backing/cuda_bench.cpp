#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>

#include "../../include/cuda_memory.hpp"

int main() {
    using namespace vram::cuda_mem;

    if (!init()) {
        std::cerr << "CUDA backend init failed" << std::endl;
        return 1;
    }

    size_t alloc = increase_pool(4 * 1024 * 1024); // 4MB
    std::cerr << "allocated " << alloc << " bytes of device memory" << std::endl;

    auto blk = allocate();
    if (!blk) {
        std::cerr << "no block available" << std::endl;
        return 1;
    }

    std::vector<char> data(block::size, 0x5A);
    std::vector<char> out(block::size);

    auto t0 = std::chrono::steady_clock::now();
    blk->write(0, block::size, data.data(), false);
    blk->sync();
    auto t1 = std::chrono::steady_clock::now();

    blk->read(0, block::size, out.data());
    auto t2 = std::chrono::steady_clock::now();

    std::chrono::duration<double> w = t1 - t0;
    std::chrono::duration<double> r = t2 - t1;

    std::cout << "write time: " << w.count() << " s\n";
    std::cout << "read time: " << r.count() << " s\n";

    // simple verification
    if (memcmp(data.data(), out.data(), block::size) == 0) std::cout << "data OK" << std::endl;
    else std::cout << "data MISMATCH" << std::endl;

    return 0;
}
