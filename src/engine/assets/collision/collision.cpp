// src/engine/assets/collision/collision.cpp

#include <engine/assets/collision/collision.h>

#include <cmath>
#include <utility>

namespace wz::engine::assets
{
    namespace
    {
        bool valid_bounds(const float min[3], const float max[3]) noexcept
        {
            return min[0] <= max[0]
                && min[1] <= max[1]
                && min[2] <= max[2];
        }

        // A heightfield's own numbers were never checked for finiteness, so a
        // NaN sample or vertical_scale reached the runtime sampler, which
        // returned hit=true with position=(nan,nan,nan) -- and the terrain-stick
        // consumer writes that straight into a node transform while its
        // `std::abs(nan - y) > 1e-6f` test reports "nothing changed" (#314,
        // C1-C3). Two sibling compilers already do exactly this check
        // (audio_clip_compilers.cpp validate_finite, scalar_field_compilers.cpp);
        // collision was the one that never got it.
        //
        // It lives in valid() rather than in the compiler because valid() is the
        // choke point BOTH the compile path and the disk-cache load path pass
        // through -- a poisoned cache blob would bypass a compiler-side check.
        // valid() is only ever called at compile/load boundaries, never per
        // frame, so the O(n) scan is paid once per asset.
        bool all_finite(const float* values, size_t count) noexcept
        {
            for (size_t i = 0; i < count; ++i) {
                if (!std::isfinite(values[i])) {
                    return false;
                }
            }
            return true;
        }
    }

    bool CollisionAssetData::valid() const noexcept
    {
        if (source_asset == wz::asset::AssetKey{}) {
            return false;
        }
        if (!valid_bounds(bounds_min, bounds_max)) {
            return false;
        }

        switch (shape_kind) {
        case CollisionShapeKind::Bounds:
            return true;

        case CollisionShapeKind::TriangleMesh:
            return !points.empty()
                && !indices.empty()
                && (indices.size() % 3u) == 0u;

        case CollisionShapeKind::TerrainHeightField:
            return resolution_x > 0
                && resolution_y > 0
                && size[0] > 0.0f
                && size[1] > 0.0f
                && height_samples.size()
                    == static_cast<size_t>(resolution_x) * resolution_y
                && std::isfinite(vertical_scale)
                && std::isfinite(base_height)
                && all_finite(origin, 2u)
                && all_finite(size, 2u)
                && all_finite(height_samples.data(), height_samples.size());

        case CollisionShapeKind::TerrainMeshSurface:
            if (mesh == wz::asset::AssetKey{} || source_triangle_count == 0) {
                return false;
            }
            if (!supports_overlap_query) {
                return indices.empty()
                    && triangle_bounds.empty()
                    && surface_grid.cell_triangle_indices.empty();
            }
            return !points.empty()
                && !indices.empty()
                && (indices.size() % 3u) == 0u
                && triangle_bounds.size() == indices.size() / 3u;
        }

        return false;
    }

    CollisionAssetTable::CollisionAssetTable()
    {
        slots_.push_back(Slot{});
    }

    wz::asset::ResourceHandle CollisionAssetTable::add(
        CollisionAssetData collision)
    {
        Slot slot{};
        slot.epoch = 1;
        slot.occupied = true;
        slot.collision = std::move(collision);

        const uint32_t id = static_cast<uint32_t>(slots_.size());
        slots_.push_back(std::move(slot));

        wz::asset::ResourceHandle handle{};
        handle.id = id;
        handle.epoch = 1;
        return handle;
    }

    const CollisionAssetData* CollisionAssetTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        if (handle.id >= slots_.size()) {
            return nullptr;
        }

        const Slot& slot = slots_[handle.id];
        if (!slot.occupied || slot.epoch != handle.epoch) {
            return nullptr;
        }

        return &slot.collision;
    }

    void CollisionAssetTable::destroy()
    {
        slots_.clear();
        slots_.push_back(Slot{});
    }
}
