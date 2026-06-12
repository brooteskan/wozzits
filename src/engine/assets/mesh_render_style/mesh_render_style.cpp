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

    bool MeshFieldVisualizationStyle::valid() const noexcept
    {
        if (!enabled) {
            return true;
        }

        return channel_id != 0u
            && std::isfinite(value_min)
            && std::isfinite(value_max)
            && value_min < value_max
            && std::isfinite(gamma)
            && gamma > 0.0f;
    }

    bool MeshMaskRule::valid() const noexcept
    {
        if (!enabled) {
            return true;
        }
        if (input_channel_id == 0u
            || !std::isfinite(lo)
            || !std::isfinite(hi))
        {
            return false;
        }
        for (float channel : color) {
            if (!std::isfinite(channel)) {
                return false;
            }
        }
        return lo <= hi;
    }

    bool MeshMaskRenderStyleData::valid() const noexcept
    {
        if (!enabled) {
            return true;
        }
        if (domain != MeshMaskDomain::Face
            || projection_mode != MeshMaskProjectionMode::Direct
            || (overlap_mode != MeshMaskOverlapMode::Priority
                && overlap_mode != MeshMaskOverlapMode::AlphaBlend)
            || rules.empty())
        {
            return false;
        }
        for (float channel : unmatched_color) {
            if (!std::isfinite(channel)) {
                return false;
            }
        }
        return std::all_of(
            rules.begin(),
            rules.end(),
            [](const MeshMaskRule& rule) { return rule.valid(); });
    }

    bool MeshRenderStyleData::valid() const noexcept
    {
        return wireframe.valid()
            && surface.valid()
            && std::isfinite(alpha)
            && alpha >= 0.0f
            && alpha <= 1.0f
            && field_visualization.valid()
            && mask.valid()
            && !(field_visualization.enabled && mask.enabled);
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
