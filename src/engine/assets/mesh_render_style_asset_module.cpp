#include <engine/assets/mesh_render_style_asset_module.h>

#include <engine/assets/key_factories/mesh_render_style.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <cmath>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        bool finite_color(const float (&color)[4]) noexcept
        {
            for (float channel : color) {
                if (!std::isfinite(channel)) {
                    return false;
                }
            }
            return true;
        }

        std::string mesh_render_style_invalid_reason(
            const MeshRenderStyleData& style)
        {
            if (!style.wireframe.valid()) {
                return "wireframe layer has invalid color or emissive strength";
            }
            if (!style.surface.valid()) {
                return "surface layer has invalid color or emissive strength";
            }
            if (!std::isfinite(style.alpha)
                || style.alpha < 0.0f
                || style.alpha > 1.0f)
            {
                return "alpha must be finite and in [0, 1]";
            }
            if (style.field_visualization.enabled) {
                if (style.field_visualization.channel_id == 0u) {
                    return "field visualization channel_id is 0";
                }
                if (!std::isfinite(style.field_visualization.value_min)
                    || !std::isfinite(style.field_visualization.value_max)
                    || style.field_visualization.value_min
                        >= style.field_visualization.value_max)
                {
                    return "field visualization value range is invalid";
                }
                if (!std::isfinite(style.field_visualization.gamma)
                    || style.field_visualization.gamma <= 0.0f)
                {
                    return "field visualization gamma must be positive";
                }
            }
            if (style.mask.enabled) {
                if (style.mask.domain != MeshMaskDomain::Face
                    && style.mask.domain != MeshMaskDomain::Vertex)
                {
                    return "mesh mask domain must be face or vertex";
                }
                if (style.mask.projection_mode
                    != MeshMaskProjectionMode::Direct)
                {
                    return "mesh mask projection mode must be direct";
                }
                if (style.mask.overlap_mode != MeshMaskOverlapMode::Priority
                    && style.mask.overlap_mode
                        != MeshMaskOverlapMode::AlphaBlend)
                {
                    return "mesh mask overlap mode is unsupported";
                }
                if (style.mask.rules.empty()) {
                    return "mesh mask is enabled but has no rules";
                }
                if (style.mask.rules.size() > kMaxMeshMaskRules) {
                    return "mesh mask has too many rules";
                }
                if (!finite_color(style.mask.unmatched_color)) {
                    return "mesh mask unmatched color is invalid";
                }
                for (size_t i = 0; i < style.mask.rules.size(); ++i) {
                    const MeshMaskRule& rule = style.mask.rules[i];
                    if (!rule.enabled) {
                        continue;
                    }
                    if (rule.input_channel_id == 0u) {
                        return "mesh mask rule " + std::to_string(i)
                            + " has input_channel_id 0";
                    }
                    if (!std::isfinite(rule.lo)
                        || !std::isfinite(rule.hi)
                        || rule.lo > rule.hi)
                    {
                        return "mesh mask rule " + std::to_string(i)
                            + " has invalid lo/hi range";
                    }
                    if (!finite_color(rule.color)) {
                        return "mesh mask rule " + std::to_string(i)
                            + " has invalid color";
                    }
                }
            }
            if (style.field_visualization.enabled && style.mask.enabled) {
                return "field visualization and mesh mask are both enabled";
            }
            return "unknown invalid mesh render style";
        }
    }

    MeshRenderStyleAssetModule::MeshRenderStyleAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        MeshRenderStyleTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    MeshRenderStyleAsset
    MeshRenderStyleAssetModule::create_mesh_render_style(
        const MeshRenderStyleDesc& desc)
    {
        if (desc.name.empty()) {
            logger_.error("mesh render style has empty name");
            return {};
        }

        if (!desc.style.valid()) {
            logger_.error(
                "mesh render style is invalid: "
                + mesh_render_style_invalid_reason(desc.style)
                + ": " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key =
            make_mesh_render_style_key(desc.name, desc.style);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeMeshRenderStyle;
        node.schema = kMeshRenderStyleSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = MeshRenderStyleCompileDesc{
            .style = desc.style,
        };

        if (!system_.register_asset(std::move(node))) {
            return MeshRenderStyleAsset{ .output = key };
        }

        return MeshRenderStyleAsset{ .output = key };
    }

    MeshRenderStyleHandle
    MeshRenderStyleAssetModule::get_mesh_render_style(
        const MeshRenderStyleAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        MeshRenderStyleHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }

        if (!out.valid()) {
            logger_.error("mesh render style handle not found");
        }

        return out;
    }

    const MeshRenderStyleData*
    MeshRenderStyleAssetModule::get_mesh_render_style_data(
        MeshRenderStyleHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }

        return table_.get(handle.handle);
    }
}
