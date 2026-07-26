// src/engine/assets/texture_asset_module.cpp

#include <engine/assets/texture_asset_module.h>

#include <engine/assets/key_factories/texture.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wz::engine::assets
{
    TextureAssetModule::TextureAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        TextureTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    TextureAsset TextureAssetModule::create_from_file(
        const TextureFromFileDesc& desc)
    {
        TextureAsset out{};

        if (desc.source_file == wz::asset::AssetKey{}) {
            logger_.error(
                "texture from file has empty source file key: " + desc.name);
            return out;
        }

        const int usage_index = static_cast<int>(desc.usage);
        const int color_space_index = static_cast<int>(desc.color_space);

        const wz::asset::AssetKey key =
            make_texture_from_file_key(
                desc.source_file, usage_index, color_space_index);

        // The graph/editor path reads the dials from a ParamBlock; build the same
        // block so a typed create and an authored node compile identically.
        wz::asset::ParamBlock params;
        params.values["usage"] = static_cast<int64_t>(usage_index);
        params.values["color_space"] = static_cast<int64_t>(color_space_index);

        wz::asset::AssetNode node{};
        node.key     = key;
        node.type    = kAssetTypeTexture;
        node.schema  = kTextureFromFileSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = params;

        if (!system_.register_asset(std::move(node), { desc.source_file })) {
            logger_.error("failed to register texture from file: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    TextureAsset TextureAssetModule::create_render_target(
        const RenderTargetTextureDesc& desc)
    {
        if (desc.name.empty()) {
            // The name is what distinguishes two targets of the same size, so
            // an unnamed one would silently alias the next unnamed one.
            logger_.error("render target texture has empty name");
            return {};
        }
        // The compiler rejects a degenerate extent with a reason the inspector
        // can show; catching it here too means the typed caller learns at the
        // create rather than at resolve.
        constexpr uint32_t kMaxExtent = 8192u;
        if (desc.width == 0u || desc.height == 0u
            || desc.width > kMaxExtent || desc.height > kMaxExtent)
        {
            logger_.error(
                "render target texture dimensions must be within 1.."
                + std::to_string(kMaxExtent) + ": " + desc.name);
            return {};
        }

        const int color_space_index = static_cast<int>(desc.color_space);

        const wz::asset::AssetKey key = make_render_target_texture_key(
            desc.name, desc.width, desc.height, color_space_index);

        // Same ParamBlock the graph/editor path authors, so a typed create and
        // an authored node compile through the identical compiler branch.
        wz::asset::ParamBlock params;
        params.values["width"] = static_cast<int64_t>(desc.width);
        params.values["height"] = static_cast<int64_t>(desc.height);
        params.values["color_space"] = static_cast<int64_t>(color_space_index);

        wz::asset::AssetNode node{};
        node.key     = key;
        node.type    = kAssetTypeTexture;
        node.schema  = kRenderTargetTextureSchema;
        node.stage   = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta    = params;

        // A repeat create of the same named target is the same asset, so a
        // rejected registration (key already present) is success, not failure --
        // the same convention create_render_binding_layout uses.
        (void)system_.register_asset(std::move(node));

        return TextureAsset{ .output = key };
    }

    TextureHandle TextureAssetModule::get_texture(
        const TextureAsset& asset) const
    {
        if (!asset.valid()) {
            return {};
        }

        TextureHandle out{};
        if (const auto* compiled = system_.find_compiled(asset.output)) {
            out.handle = compiled->handle;
        }
        if (!out.valid()) {
            logger_.error("texture handle not found");
        }
        return out;
    }

    const TextureData* TextureAssetModule::get_texture_data(
        TextureHandle handle) const
    {
        if (!handle.valid()) {
            return nullptr;
        }
        return table_.get(handle.handle);
    }
}
