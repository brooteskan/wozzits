#pragma once

// wozzits/rhi/gpu_resource_registry.h
//
// The device-scoped owner of every GPU resource. This is the single structure
// that replaces the old renderer's scattered, inconsistent caches (the
// renderable GPU cache, the resident-field table, the pipeline caches, and the
// translation-unit-static leaked buffers). It owns the four things those did
// inconsistently or not at all:
//
//   - identity & dedup: find-or-create by (asset_id, variant);
//   - lifetime: PRECISE deferred release keyed on the GPU timeline value that
//     last referenced a resource — not a fixed 3-frame latency guess;
//   - mutability: in-place update for CPU-writable resources (the #145
//     per-frame refresh path), with a checkable failure for GPU-only ones;
//   - device loss: a SINGLE sweep invalidates every handle at once, instead of
//     six tables plus a leaked static that nothing can find.
//
// It owns none of the actual GPU mechanics — those are delegated to GpuBackend
// — so it stays device-agnostic and unit-testable with a fake backend.

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/handle.h>

#include <cstdint>
#include <vector>

namespace wz::rhi
{
    class GpuResourceRegistry
    {
    public:
        // The backend must outlive the registry.
        explicit GpuResourceRegistry(GpuBackend& backend)
            : backend_(&backend)
        {
        }

        // Find-or-create. A non-anonymous identity that already resolves to a
        // live resource returns the existing handle with no second backend
        // create. Anonymous identities (asset_id == 0) always create.
        GpuResourceHandle acquire(const GpuResourceDesc& desc)
        {
            if (!desc.identity.anonymous()) {
                const GpuResourceHandle existing = find(desc.identity);
                if (existing.valid()) {
                    return existing;
                }
            }

            const BackendResource backend = backend_->create(desc);
            if (!backend.valid()) {
                return {};
            }

            const GpuResourceHandle handle =
                slots_.insert(Entry{ GpuResource{ desc, backend }, /*last_use*/ 0,
                                     /*pending_release*/ false });
            if (!desc.identity.anonymous()) {
                index_.push_back(IndexEntry{ desc.identity, handle });
            }
            return handle;
        }

        // Resolve an existing resident resource by identity; null handle if
        // none. (A pending-release resource is treated as gone — see release.)
        [[nodiscard]] GpuResourceHandle find(const ResourceIdentity& identity) const
        {
            for (const IndexEntry& entry : index_) {
                if (entry.identity == identity && slots_.get(entry.handle)) {
                    return entry.handle;
                }
            }
            return {};
        }

        // Bind-time access. Returns nullptr for a stale handle (released,
        // collected, or invalidated by device loss) — a checkable miss.
        [[nodiscard]] const GpuResource* get(GpuResourceHandle handle) const
        {
            const Entry* entry = slots_.get(handle);
            return entry ? &entry->resource : nullptr;
        }

        // In-place content update — the #145 per-frame refresh. The handle and
        // identity stay stable; only the bytes change. Returns false for a
        // stale handle or a GPU-only resource (cpu_access None): a checkable
        // error, never a silent no-op.
        bool update(GpuResourceHandle handle,
                    const void* data,
                    uint64_t size,
                    uint64_t offset = 0)
        {
            Entry* entry = slots_.get(handle);
            if (!entry) {
                return false;
            }
            if (entry->resource.desc.cpu_access == ResourceCpuAccess::None) {
                return false;
            }
            return backend_->write(entry->resource.backend, data, size, offset);
        }

        // Record the GPU timeline value of the most recent submission that
        // referenced this resource. collect() uses it for precise reclamation.
        void touch(GpuResourceHandle handle, uint64_t timeline_value)
        {
            if (Entry* entry = slots_.get(handle)) {
                if (timeline_value > entry->last_use) {
                    entry->last_use = timeline_value;
                }
            }
        }

        // Queue a resource for release. It is removed from identity lookup
        // immediately (so re-acquire builds a fresh one) but its backing is not
        // destroyed until the GPU has passed its last-use timeline.
        void release(GpuResourceHandle handle)
        {
            Entry* entry = slots_.get(handle);
            if (!entry || entry->pending_release) {
                return;
            }
            entry->pending_release = true;
            pending_.push_back(handle);
            remove_from_index(handle);
        }

        // Reclaim every pending resource whose last-use timeline has been
        // passed by the GPU. Resources still in flight are kept. This is the
        // precise replacement for the fixed-latency deferred queue.
        void collect(uint64_t completed_timeline_value)
        {
            size_t write = 0;
            for (size_t read = 0; read < pending_.size(); ++read) {
                const GpuResourceHandle handle = pending_[read];
                Entry* entry = slots_.get(handle);
                if (!entry) {
                    continue;  // already gone (e.g. device loss)
                }
                if (entry->last_use <= completed_timeline_value) {
                    backend_->destroy(entry->resource.backend);
                    slots_.erase(handle);
                }
                else {
                    pending_[write++] = handle;  // still referenced by the GPU
                }
            }
            pending_.resize(write);
        }

        // Device loss: destroy every backend resource and invalidate every
        // handle in ONE sweep. Outstanding handles all go stale (slot
        // generations bump), so nothing can bind a resource from the dead
        // device. Bumps the device epoch for diagnostics.
        void on_device_lost()
        {
            slots_.for_each([&](GpuResourceHandle, Entry& entry) {
                backend_->destroy(entry.resource.backend);
            });
            slots_.clear();
            index_.clear();
            pending_.clear();
            ++device_epoch_;
        }

        [[nodiscard]] size_t resident_count() const noexcept
        {
            return slots_.size();
        }

        [[nodiscard]] uint64_t device_epoch() const noexcept
        {
            return device_epoch_;
        }

    private:
        struct Entry
        {
            GpuResource resource{};
            uint64_t    last_use = 0;
            bool        pending_release = false;
        };

        struct IndexEntry
        {
            ResourceIdentity  identity{};
            GpuResourceHandle handle{};
        };

        void remove_from_index(GpuResourceHandle handle)
        {
            for (size_t i = 0; i < index_.size(); ++i) {
                if (index_[i].handle == handle) {
                    index_[i] = index_.back();
                    index_.pop_back();
                    return;
                }
            }
        }

        GpuBackend*                    backend_;
        // Store an internal Entry but expose the public GpuResourceHandle.
        SlotMap<Entry, GpuResourceTag> slots_;
        // Identity -> handle, linear for now; swap to a hash map when the
        // resident set grows enough to warrant it.
        std::vector<IndexEntry>        index_;
        std::vector<GpuResourceHandle> pending_;
        uint64_t                       device_epoch_ = 0;
    };
}
