#include <engine/assets/mesh_render_style/mesh_render_style.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <cmath>

namespace wz::engine::assets
{
    MeshRenderStyleTable::MeshRenderStyleTable()
    {
        styles_.emplace_back();
        epochs_.push_back(0);
    }

    bool MeshRenderLayerStyle::valid() const noexcept
    {
        if (!std::isfinite(emissive_strength) || emissive_strength < 0.0f) {
            return false;
        }

        for (float channel : color) {
            if (!std::isfinite(channel)) {
                return false;
            }
        }

        return true;
    }

    bool MeshRenderStyleData::valid() const noexcept
    {
        return wireframe.valid()
            && surface.valid()
            && std::isfinite(alpha)
            && alpha >= 0.0f
            && alpha <= 1.0f;
    }

    wz::asset::ResourceHandle MeshRenderStyleTable::add(
        MeshRenderStyleData data)
    {
        if (!data.valid()) {
            return {};
        }

        const uint32_t id = static_cast<uint32_t>(styles_.size());
        styles_.push_back(data);
        epochs_.push_back(1);
        return wz::asset::ResourceHandle{
            .id = id,
            .epoch = epochs_[id],
            .type = kAssetTypeMeshRenderStyle,
        };
    }

    const MeshRenderStyleData* MeshRenderStyleTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (handle.type != kAssetTypeMeshRenderStyle) {
            return nullptr;
        }
        if (handle.id >= styles_.size()) {
            return nullptr;
        }
        if (epochs_[handle.id] != handle.epoch) {
            return nullptr;
        }
        return &styles_[handle.id];
    }

    void MeshRenderStyleTable::destroy()
    {
        styles_.clear();
        epochs_.clear();

        styles_.emplace_back();
        epochs_.push_back(0);
    }
}
