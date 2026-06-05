// src/engine/assets/collision/collision.cpp

#include <engine/assets/collision/collision.h>

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
                    == static_cast<size_t>(resolution_x) * resolution_y;

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
