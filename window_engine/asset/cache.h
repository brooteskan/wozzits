#pragma once

// wz/asset/cache.h
//
// Runtime asset cache: AssetKey → compiled GPUHandle.
//
// The cache is mutable, eviction-based, and entirely optional —
// discarding it only forces recompilation; it does not affect DAG correctness.
// The DAG itself (holding source/IR payloads) is the persistent source of truth.

#include "types.h"
#include <unordered_map>
#include <optional>

namespace wz::asset {

// ─── CacheEntry ───────────────────────────────────────────────────────────────

struct CacheEntry {
    AssetKey  key;
    ResourceHandle gpu_handle;
    bool      valid = false;   // false = soft-invalidated, entry still occupies slot
};

// ─── AssetCache ───────────────────────────────────────────────────────────────

class AssetCache {
public:
    explicit AssetCache(uint32_t initial_reserve = 256) {
        entries_.reserve(initial_reserve);
    }

    // ── Writes ────────────────────────────────────────────────────────────────

    // Store a successfully compiled GPU resource.
    void store(const AssetKey& key, ResourceHandle handle) {
        entries_.insert_or_assign(key, CacheEntry{ key, handle, /*valid=*/true });
    }

    // Soft-invalidate an entry: marks it stale without removing it.
    // Useful for hot-reload: invalidate → recompile → store. The slot is reused
    // so the hash table does not grow during repeated invalidation cycles.
    void invalidate(const AssetKey& key) {
        auto it = entries_.find(key);
        if (it != entries_.end()) it->second.valid = false;
    }

    // Hard-evict: removes the entry entirely and releases the slot.
    void evict(const AssetKey& key) {
        entries_.erase(key);
    }

    // Evict everything.
    void clear() { entries_.clear(); }

    // ── Reads ─────────────────────────────────────────────────────────────────

    // Returns the GPU handle on hit, nullopt on miss or soft-invalidated entry.
    std::optional<ResourceHandle> lookup(const AssetKey& key) const {
        auto it = entries_.find(key);
        if (it == entries_.end() || !it->second.valid) return std::nullopt;
        return it->second.gpu_handle;
    }

    bool contains(const AssetKey& key) const {
        auto it = entries_.find(key);
        return it != entries_.end() && it->second.valid;
    }

    // ── Diagnostics ───────────────────────────────────────────────────────────

    // Total slots (valid + soft-invalidated).
    uint32_t size()  const { return static_cast<uint32_t>(entries_.size()); }
    bool     empty() const { return entries_.empty(); }

private:
    std::unordered_map<AssetKey, CacheEntry, AssetKeyHash> entries_;
};

} // namespace wz::asset
