
#include "cuda_memory.hpp"
#include <cstring>
#include <iostream>
#include <thread>

#ifdef USE_CUDA
#include <cuda_runtime.h>
#endif

namespace vram {
    namespace cuda_mem {
        static size_t device_idx = 0;

        // Device block pool
        static std::vector<void*> device_pool;
        static std::mutex device_pool_mutex;

        // Pinned host staging pool
        static std::vector<void*> staging_pool;
        static std::mutex staging_pool_mutex;

        static int staging_count = 0;

        bool init() {
#ifdef USE_CUDA
            int devcount = 0;
            if (cudaGetDeviceCount(&devcount) != cudaSuccess || devcount == 0) {
                std::cerr << "cuda_mem: no CUDA devices found" << std::endl;
                return false;
            }
            if (device_idx >= (size_t)devcount) device_idx = 0;
            cudaSetDevice((int)device_idx);
            std::cerr << "cuda_mem: initialized CUDA device " << device_idx << std::endl;
            return true;
#else
            std::cerr << "cuda_mem: compiled without USE_CUDA; backend disabled" << std::endl;
            return false;
#endif
        }

        void set_device(size_t idx) { device_idx = idx; }

        std::vector<std::string> list_devices() {
            std::vector<std::string> out;
#ifdef USE_CUDA
            int devcount = 0;
            if (cudaGetDeviceCount(&devcount) == cudaSuccess) {
                for (int i = 0; i < devcount; ++i) {
                    cudaDeviceProp prop;
                    cudaGetDeviceProperties(&prop, i);
                    out.push_back(std::string(prop.name));
                }
            }
#endif
            return out;
        }

        size_t increase_pool(size_t size) {
    #ifdef USE_CUDA
            int blocks = (int)((size + block::size - 1) / block::size);
            int allocated = 0;
            for (int i = 0; i < blocks; ++i) {
            void* devptr = nullptr;
            cudaError_t err = cudaMalloc(&devptr, block::size);
            if (err != cudaSuccess) break;
            cudaMemset(devptr, 0, block::size);
            std::lock_guard<std::mutex> lg(device_pool_mutex);
            device_pool.push_back(devptr);
            ++allocated;
            }
            return (size_t)allocated * block::size;
    #else
            (void)size;
            return 0;
    #endif
        }

        int pool_size() {
    #ifdef USE_CUDA
            std::lock_guard<std::mutex> lg(device_pool_mutex);
            return (int)device_pool.size();
    #else
            return 0;
    #endif
        }

        int pool_available() {
    #ifdef USE_CUDA
            std::lock_guard<std::mutex> lg(device_pool_mutex);
            return (int)device_pool.size();
    #else
            return 0;
    #endif
        }

        bool init_staging_pool(int count) {
    #ifdef USE_CUDA
            staging_count = count;
            for (int i = 0; i < count; ++i) {
            void* hostptr = nullptr;
            if (cudaHostAlloc(&hostptr, block::size, cudaHostAllocDefault) != cudaSuccess) {
                // cleanup
                std::lock_guard<std::mutex> lg(staging_pool_mutex);
                for (void* p : staging_pool) cudaFreeHost(p);
                staging_pool.clear();
                return false;
            }
            staging_pool.push_back(hostptr);
            }
            return true;
    #else
            (void)count;
            return false;
    #endif
        }

        void shutdown_staging_pool() {
    #ifdef USE_CUDA
            std::lock_guard<std::mutex> lg(staging_pool_mutex);
            for (void* p : staging_pool) {
            cudaFreeHost(p);
            }
            staging_pool.clear();
    #endif
        }

        block_ref allocate() {
    #ifdef USE_CUDA
            std::lock_guard<std::mutex> lg(device_pool_mutex);
            if (device_pool.empty()) return nullptr;
            void* devptr = device_pool.back();
            device_pool.pop_back();
            return std::make_shared<block>(devptr);
    #else
            return nullptr;
    #endif
        }

        // block implementation
        block::block(void* device_ptr) : impl(device_ptr) {}

        block::~block() {
#ifdef USE_CUDA
            // Return device pointer to pool for reuse
            if (impl) {
                std::lock_guard<std::mutex> lg(device_pool_mutex);
                device_pool.push_back(impl);
            }
#endif
            impl = nullptr;
        }

        void block::read(off_t offset, size_t sz, void* data) const {
            if (!impl) { memset(data, 0, sz); return; }
#ifdef USE_CUDA
            const char* src = static_cast<const char*>(impl) + offset;
            cudaMemcpy(data, src, sz, cudaMemcpyDeviceToHost);
#else
            memset(data, 0, sz);
#endif
        }

        void block::write(off_t offset, size_t sz, const void* data, bool async) {
            if (!impl) return;
#ifdef USE_CUDA
            char* dst = static_cast<char*>(impl) + offset;
            if (!async) {
                cudaMemcpy(dst, data, sz, cudaMemcpyHostToDevice);
            } else {
                // Try to obtain a pinned staging buffer
                void* staging = nullptr;
                {
                    std::lock_guard<std::mutex> lg(staging_pool_mutex);
                    if (!staging_pool.empty()) {
                        staging = staging_pool.back();
                        staging_pool.pop_back();
                    }
                }

                if (!staging) {
                    // allocate a temporary pinned buffer
                    if (cudaHostAlloc(&staging, block::size, cudaHostAllocDefault) != cudaSuccess) {
                        // fallback to synchronous copy
                        cudaMemcpy(dst, data, sz, cudaMemcpyHostToDevice);
                        return;
                    }
                }

                // copy into pinned buffer then async copy to device
                memcpy(staging, data, sz);

                cudaStream_t stream;
                cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
                cudaMemcpyAsync(dst, staging, sz, cudaMemcpyHostToDevice, stream);

                // record an event and launch a small cleanup thread to return staging
                cudaEvent_t ev;
                cudaEventCreateWithFlags(&ev, cudaEventDisableTiming);
                cudaEventRecord(ev, stream);

                std::thread([staging, ev]() mutable {
                    cudaEventSynchronize(ev);
                    cudaEventDestroy(ev);
                    // return staging to pool
                    std::lock_guard<std::mutex> lg(staging_pool_mutex);
                    staging_pool.push_back(staging);
                }).detach();

                cudaStreamDestroy(stream);
            }
#endif
        }

        void block::sync() {
#ifdef USE_CUDA
            cudaDeviceSynchronize();
#endif
        }
    }
}
