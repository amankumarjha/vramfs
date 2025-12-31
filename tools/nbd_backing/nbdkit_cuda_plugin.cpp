// Simple nbdkit plugin forwarding read/write to the CUDA backend (prototype)
#include <cstring>
// nbdkit plugin forwarding read/write to the CUDA backend (prototype)
#include <cstring>
#include <cstdint>
#include <errno.h>
#include "cuda_memory.hpp"
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>

// Request API v2 to get pread/pwrite with flags
#define NBDKIT_API_VERSION 2
#ifndef THREAD_MODEL
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL
#endif
extern "C" {
#include <nbdkit-plugin.h>
}

using namespace vram::cuda_mem;

static bool backend_inited = false;
static int64_t plugin_size_bytes = 64LL * 1024 * 1024; // default 64MiB

struct BlockEntry {
    block_ref b;
    std::mutex m;
};

static std::vector<std::shared_ptr<BlockEntry>> backing_map;
static std::mutex backing_map_mutex; /* protects vector resize and structure */

static size_t num_blocks() {
    return (plugin_size_bytes + block::size - 1) / block::size;
}

static std::atomic<size_t> total_allocated_blocks{0};
static size_t max_allocated_blocks = 0; /* 0 = no limit */

static void ensure_init()
{
    if (backend_inited) return;
    if (!vram::cuda_mem::init()) {
        nbdkit_error("CUDA backend init failed");
        return;
    }
    vram::cuda_mem::init_staging_pool(8);
    // Try to increase pool to cover plugin_size_bytes
    size_t blocks = (plugin_size_bytes + block::size - 1) / block::size;
    vram::cuda_mem::increase_pool(blocks);
    // initialize backing map
    {
        std::lock_guard<std::mutex> lg(backing_map_mutex);
        backing_map.clear();
        backing_map.resize(blocks);
    }
    backend_inited = true;
}

static int64_t vram_get_size(void *handle)
{
    ensure_init();
    return plugin_size_bytes;
}

static int vram_can_write(void *handle)
{
    (void)handle;
    return 1; // writable
}

static int vram_can_fua(void *handle)
{
    (void)handle;
    return 0; // not supporting FUA in prototype
}

static void *vram_open(int readonly)
{
    (void)readonly;
    ensure_init();
    return NBDKIT_HANDLE_NOT_NEEDED;
}

static void vram_close(void *handle)
{
    (void)handle;
}

static int vram_pread(void *handle, void *buf, uint32_t count, uint64_t offset, uint32_t flags)
{
    ensure_init();
    if (!backend_inited) return -EIO;

    if (offset + (uint64_t)count > (uint64_t)plugin_size_bytes) return -EIO;

    uint8_t *out = (uint8_t*)buf;
    uint64_t pos = offset;
    uint32_t remaining = count;

    while (remaining) {
        size_t block_idx = pos / block::size;
        size_t block_off = pos % block::size;
        size_t toread = std::min<size_t>(remaining, block::size - block_off);

        std::shared_ptr<BlockEntry> entry;
        {
            std::lock_guard<std::mutex> lg(backing_map_mutex);
            if (block_idx < backing_map.size()) entry = backing_map[block_idx];
        }

        if (!entry || !entry->b) {
            memset(out, 0, toread);
        } else {
            std::lock_guard<std::mutex> lg(entry->m);
            entry->b->read((off_t)block_off, toread, out);
        }

        out += toread;
        pos += toread;
        remaining -= (uint32_t)toread;
    }

    return 0;
}

static int vram_pwrite(void *handle, const void *buf, uint32_t count, uint64_t offset, uint32_t flags)
{
    ensure_init();
    if (!backend_inited) return -EIO;

    if (offset + (uint64_t)count > (uint64_t)plugin_size_bytes) return -EIO;

    const uint8_t *in = (const uint8_t*)buf;
    uint64_t pos = offset;
    uint32_t remaining = count;

    while (remaining) {
        size_t block_idx = pos / block::size;
        size_t block_off = pos % block::size;
        size_t towrite = std::min<size_t>(remaining, block::size - block_off);

        std::shared_ptr<BlockEntry> entry;
        {
            std::lock_guard<std::mutex> lg(backing_map_mutex);
            if (block_idx >= backing_map.size()) return -EIO;
            entry = backing_map[block_idx];
        }

        // allocate block entry if missing
        if (!entry) {
            std::lock_guard<std::mutex> lg(backing_map_mutex);
            if (!backing_map[block_idx]) backing_map[block_idx] = std::make_shared<BlockEntry>();
            entry = backing_map[block_idx];
        }

        // allocate device block if missing and perform synchronous write
        {
            std::lock_guard<std::mutex> lg(entry->m);
            if (!entry->b) {
                entry->b = allocate();
                if (!entry->b) return -ENOSPC;
                total_allocated_blocks.fetch_add(1);
            }
            entry->b->write((off_t)block_off, towrite, in, false);
            entry->b->sync();
        }

        in += towrite;
        pos += towrite;
        remaining -= (uint32_t)towrite;
    }
    return 0;
}

static int vram_flush(void *handle, uint32_t flags)
{
    (void)handle; (void)flags;
    if (!backend_inited) return -EIO;
    // ensure all pending writes complete
    std::lock_guard<std::mutex> lg(backing_map_mutex);
    for (auto &entry_ptr : backing_map) {
        if (!entry_ptr) continue;
        std::lock_guard<std::mutex> lg2(entry_ptr->m);
        if (entry_ptr->b) entry_ptr->b->sync();
    }
    return 0;
}

static int vram_block_size(void *handle, uint32_t *minimum, uint32_t *preferred, uint32_t *maximum)
{
    (void)handle;
    uint32_t m = (uint32_t)block::size;
    if (minimum) *minimum = m;
    if (preferred) *preferred = m;
    if (maximum) *maximum = m;
    nbdkit_debug("vram-cuda: block_size reply min=%u pref=%u max=%u", m, m, m);
    return 0;
}

static int vram_can_multi_conn(void *handle)
{
    (void)handle;
    return 1;
}

static int vram_can_flush(void *handle)
{
    (void)handle;
    return 1;
}

static struct nbdkit_plugin plugin = {
    /* core name fields */
    .name = "vram-cuda",
    .longname = "VRAM-backed nbdkit plugin (CUDA prototype)",
    .version = "0.1",
    .description = "Prototype plugin exposing GPU VRAM as a block device",

    /* lifecycle */
    .load = NULL,
    .unload = NULL,

    /* config */
    .config = NULL,
    .config_complete = NULL,
    .config_help = NULL,

    /* connection handling */
    .open = vram_open,
    .close = vram_close,

    /* size and capabilities */
    .get_size = vram_get_size,
    .can_write = vram_can_write,
    .can_flush = vram_can_flush,
    .is_rotational = NULL,
    .can_trim = NULL,

    /* v1 placeholders (_pread_v1 etc) */
    ._pread_v1 = NULL,
    ._pwrite_v1 = NULL,
    ._flush_v1 = NULL,
    ._trim_v1 = NULL,
    ._zero_v1 = NULL,

    .errno_is_preserved = 0,

    .dump_plugin = NULL,

    .can_zero = NULL,
    .can_fua = vram_can_fua,

    /* API v2 handlers */
    .pread = vram_pread,
    .pwrite = vram_pwrite,
    .flush = vram_flush,
    .trim = NULL,
    .zero = NULL,

    .magic_config_key = NULL,

    .can_multi_conn = vram_can_multi_conn,

    .can_extents = NULL,
    .extents = NULL,
    .can_cache = NULL,
    .cache = NULL,

    .thread_model = NULL,

    .can_fast_zero = NULL,

    .preconnect = NULL,

    .get_ready = NULL,
    .after_fork = NULL,

    .list_exports = NULL,
    .default_export = NULL,
    .export_description = NULL,

    .cleanup = NULL,

    .block_size = vram_block_size,
};

NBDKIT_REGISTER_PLUGIN (plugin)
