// Minimal CUDA memory backend interface (scaffold)
#ifndef VRAM_CUDA_MEMORY_HPP
#define VRAM_CUDA_MEMORY_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>

namespace vram {
    namespace cuda_mem {
        class block;
        typedef std::shared_ptr<block> block_ref;

        // Initialize CUDA backend; returns true on success
        bool init();

        // Set device index
        void set_device(size_t idx);

        // Returns list of devices (names)
        std::vector<std::string> list_devices();

        // Pool management
        size_t increase_pool(size_t size);
        int pool_size();
        int pool_available();

        // Pinned staging pool (host buffers) - used for async transfers
        bool init_staging_pool(int count = 8);
        void shutdown_staging_pool();

        // Allocate block (returns nullptr if none available)
        block_ref allocate();

        // Block abstraction
        class block {
        public:
            static const size_t size = 64 * 1024; // must be <= 64KiB for nbdkit

            // Construct with an allocated device pointer
            explicit block(void* device_ptr);
            ~block();

            void read(off_t offset, size_t size, void* data) const;
            void write(off_t offset, size_t size, const void* data, bool async = false);
            void sync();

        private:
            // Implementation-specific handle (device pointer)
            void* impl = nullptr;
        };
    }
}

#endif
