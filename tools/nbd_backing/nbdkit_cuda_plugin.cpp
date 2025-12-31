// Simple nbdkit plugin forwarding read/write to the CUDA backend (cleaned)
#include <cstring>
#include <cstdint>
#include <errno.h>
#include "cuda_memory.hpp"
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

// Request API v2 to get pread/pwrite with flags
#define NBDKIT_API_VERSION 2
#ifndef THREAD_MODEL
#define THREAD_MODEL NBDKIT_THREAD_MODEL_PARALLEL
#endif
extern "C"
{
#include <nbdkit-plugin.h>
}

using namespace vram::cuda_mem;

static bool backend_inited = false;
static int64_t plugin_size_bytes = 0; /* 0 = auto-detect */
static const size_t SAFETY_RESERVE = 256ULL * 1024 * 1024;

struct BlockEntry { block_ref b; std::mutex m; };
static std::vector<std::shared_ptr<BlockEntry>> backing_map;
static std::mutex backing_map_mutex;
static std::atomic<size_t> total_allocated_blocks{0};

static int64_t parse_size_str(const char *s)
{
    if (!s) return 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 0);
    if (end == s) return 0;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end == '\0') return v;
    char suf = *end;
    if (suf == 'G' || suf == 'g') return v * 1024LL * 1024LL * 1024LL;
    if (suf == 'M' || suf == 'm') return v * 1024LL * 1024LL;
    if (suf == 'K' || suf == 'k') return v * 1024LL;
    return v;
}

static int vram_config(const char *key, const char *value)
{
    if (!strcmp(key, "size")) {
        int64_t parsed = parse_size_str(value);
        if (parsed <= 0) { nbdkit_error("invalid size '%s'", value); return -1; }
        plugin_size_bytes = parsed;
        return 0;
    }
    nbdkit_error("unknown config key '%s'", key);
    return -1;
}

static int vram_config_complete(void) { return 0; }

static void ensure_init()
{
    if (backend_inited) return;
    if (!vram::cuda_mem::init()) { nbdkit_error("CUDA backend init failed"); return; }
    vram::cuda_mem::init_staging_pool(8);
    if (plugin_size_bytes == 0) {
        size_t total = vram::cuda_mem::total_device_memory();
        if (total == 0) { nbdkit_error("unable to query device memory for auto-detect"); return; }
        size_t use = (total > SAFETY_RESERVE) ? (total - SAFETY_RESERVE) : total;
        plugin_size_bytes = (int64_t)use;
        nbdkit_debug("vram-cuda: auto-detected device total=%zu; using %lld bytes", total, (long long)plugin_size_bytes);
    }
    size_t allocated = vram::cuda_mem::increase_pool((size_t)plugin_size_bytes);
    nbdkit_debug("vram-cuda: increase_pool requested=%lld allocated=%zu", (long long)plugin_size_bytes, allocated);
    {
        std::lock_guard<std::mutex> lg(backing_map_mutex);
        size_t blocks = (plugin_size_bytes + block::size - 1) / block::size;
        backing_map.clear(); backing_map.resize(blocks);
    }
    backend_inited = true;
}

static int64_t vram_get_size(void *handle) { (void)handle; ensure_init(); return plugin_size_bytes; }
static int vram_can_write(void *handle) { (void)handle; return 1; }
static int vram_can_fua(void *handle) { (void)handle; return 0; }
static void *vram_open(int readonly) { (void)readonly; ensure_init(); return NBDKIT_HANDLE_NOT_NEEDED; }
static void vram_close(void *handle) { (void)handle; }

static int vram_pread(void *handle, void *buf, uint32_t count, uint64_t offset, uint32_t flags)
{
    (void)handle; (void)flags; ensure_init(); if (!backend_inited) return -EIO;
    if (offset + (uint64_t)count > (uint64_t)plugin_size_bytes) return -EIO;
    uint8_t *out = (uint8_t *)buf; uint64_t pos = offset; uint32_t remaining = count;
    while (remaining) {
        size_t block_idx = pos / block::size; size_t block_off = pos % block::size;
        size_t toread = std::min<size_t>(remaining, block::size - block_off);
        std::shared_ptr<BlockEntry> entry;
        { std::lock_guard<std::mutex> lg(backing_map_mutex); if (block_idx < backing_map.size()) entry = backing_map[block_idx]; }
        if (!entry || !entry->b) memset(out, 0, toread);
        else { std::lock_guard<std::mutex> lg(entry->m); entry->b->read((off_t)block_off, toread, out); }
        out += toread; pos += toread; remaining -= (uint32_t)toread;
    }
    return 0;
}

static int vram_pwrite(void *handle, const void *buf, uint32_t count, uint64_t offset, uint32_t flags)
{
    (void)handle; (void)flags; ensure_init(); if (!backend_inited) return -EIO;
    if (offset + (uint64_t)count > (uint64_t)plugin_size_bytes) return -EIO;
    const uint8_t *in = (const uint8_t *)buf; uint64_t pos = offset; uint32_t remaining = count;
    while (remaining) {
        size_t block_idx = pos / block::size; size_t block_off = pos % block::size;
        size_t towrite = std::min<size_t>(remaining, block::size - block_off);
        std::shared_ptr<BlockEntry> entry;
        { std::lock_guard<std::mutex> lg(backing_map_mutex); if (block_idx >= backing_map.size()) return -EIO; entry = backing_map[block_idx]; }
        if (!entry) { std::lock_guard<std::mutex> lg(backing_map_mutex); if (!backing_map[block_idx]) backing_map[block_idx] = std::make_shared<BlockEntry>(); entry = backing_map[block_idx]; }
        {
            std::lock_guard<std::mutex> lg(entry->m);
            if (!entry->b) { entry->b = allocate(); if (!entry->b) return -ENOSPC; total_allocated_blocks.fetch_add(1); }
            entry->b->write((off_t)block_off, towrite, in, false);
            entry->b->sync();
        }
        in += towrite; pos += towrite; remaining -= (uint32_t)towrite;
    }
    return 0;
}

static int vram_flush(void *handle, uint32_t flags) { (void)handle; (void)flags; if (!backend_inited) return -EIO; std::lock_guard<std::mutex> lg(backing_map_mutex); for (auto &entry_ptr : backing_map) { if (!entry_ptr) continue; std::lock_guard<std::mutex> lg2(entry_ptr->m); if (entry_ptr->b) entry_ptr->b->sync(); } return 0; }
static int vram_block_size(void *handle, uint32_t *minimum, uint32_t *preferred, uint32_t *maximum) { (void)handle; uint32_t m = (uint32_t)block::size; if (minimum) *minimum = m; if (preferred) *preferred = m; if (maximum) *maximum = m; nbdkit_debug("vram-cuda: block_size reply min=%u pref=%u max=%u", m, m, m); return 0; }
static int vram_can_multi_conn(void *handle) { (void)handle; return 1; }
static int vram_can_flush(void *handle) { (void)handle; return 1; }

static struct nbdkit_plugin plugin = {
    .name = "vram-cuda",
    .longname = "VRAM-backed nbdkit plugin (CUDA prototype)",
    .version = "0.1",
    .description = "Prototype plugin exposing GPU VRAM as a block device",
    .load = NULL,
    .unload = NULL,
    .config = vram_config,
    .config_complete = vram_config_complete,
    .config_help = "size=<bytes|K|M|G>    Export size (e.g. 4G). If omitted, auto-detect device_total - 256M",
    .open = vram_open,
    .close = vram_close,
    .get_size = vram_get_size,
    .can_write = vram_can_write,
    .can_flush = vram_can_flush,
    .is_rotational = NULL,
    .can_trim = NULL,
    ._pread_v1 = NULL,
    ._pwrite_v1 = NULL,
    ._flush_v1 = NULL,
    ._trim_v1 = NULL,
    ._zero_v1 = NULL,
    .errno_is_preserved = 0,
    .dump_plugin = NULL,
    .can_zero = NULL,
    .can_fua = vram_can_fua,
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

NBDKIT_REGISTER_PLUGIN(plugin)
