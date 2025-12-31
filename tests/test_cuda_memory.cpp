#include "cuda_memory.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>

using namespace vram::cuda_mem;

int main() {
    std::cout << "test: cuda_memory starting" << std::endl;

#ifdef USE_CUDA
    bool inittest = init();
    if (!inittest) {
        std::cerr << "ERROR: init() failed despite USE_CUDA" << std::endl;
        return 2;
    }
    std::cout << "init() ok" << std::endl;
#else
    bool inittest = init();
    if (inittest) {
        std::cerr << "ERROR: init() returned true but USE_CUDA not defined" << std::endl;
        return 2;
    }
    std::cout << "init() correctly disabled (no CUDA)" << std::endl;
#endif

    auto devs = list_devices();
    std::cout << "devices: " << devs.size() << std::endl;

    set_device(0);

    // Try pool operations
    size_t added = increase_pool(block::size * 2);
    std::cout << "increase_pool added bytes: " << added << std::endl;

    int psize = pool_size();
    int pavail = pool_available();
    std::cout << "pool size=" << psize << " avail=" << pavail << std::endl;

    bool stag = init_staging_pool(2);
    std::cout << "init_staging_pool returned " << stag << std::endl;

    // allocate may return nullptr when no CUDA
    auto blk = allocate();
    if (!blk) {
        std::cout << "allocate() returned null (likely no CUDA or empty pool)" << std::endl;
    } else {
        std::cout << "allocated block" << std::endl;
        // test write/read
        std::vector<char> buf(block::size);
        for (size_t i = 0; i < block::size; ++i) buf[i] = (char)(i & 0xff);
        blk->write(0, block::size, buf.data(), false);
        blk->sync();
        std::vector<char> out(block::size);
        blk->read(0, block::size, out.data());
        if (memcmp(buf.data(), out.data(), block::size) != 0) {
            std::cerr << "ERROR: read data mismatch" << std::endl;
            return 3;
        }
        std::cout << "read/write verified" << std::endl;
    }

    shutdown_staging_pool();

    std::cout << "test: cuda_memory finished" << std::endl;
    return 0;
}
