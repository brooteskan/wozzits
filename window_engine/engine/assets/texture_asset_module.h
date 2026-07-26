#pragma once

// engine/assets/texture_asset_module.h
//
// Programmatic front door for the Texture asset: register a Texture node that
// imports an image RawFile, and resolve its compiled metadata. The editor
// authors the same node via the graph; this is the typed API for engine code and
// tests. Mirror of StarCatalogAssetModule.

#include <asset/system.h>
#include <asset/types.h>
#include <engine/assets/texture/texture.h>
#include <logging/logger.h>

#include <cstdint>
#include <string>

namespace wz::engine::assets
{
    struct TextureAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct TextureHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept { return handle.valid(); }
    };

    // Import an image file as a texture. source_file names a kAssetTypeRawFile
    // (register it via the file-carrier module). usage + color_space are authored
    // dials that fold into the key, so re-tagging re-imports. color_space Auto
    // resolves from usage (Color/UI -> sRGB, Normal/Data -> Linear).
    struct TextureFromFileDesc
    {
        std::string              name;
        wz::asset::AssetKey      source_file{};
        TextureUsage             usage       = TextureUsage::Color;
        TextureColorSpaceChoice  color_space = TextureColorSpaceChoice::Auto;
    };

    // A texture with NO source file, whose extent is authored and which is made
    // resident Sampled | RenderTarget so a pass can draw into what a material
    // samples (#281). Same kAssetTypeTexture and the same
    // rhi_asset_identity(key, "texture") discriminator as an imported texture,
    // so a render binding names it wherever a texture is accepted.
    //
    // Unlike an imported texture, two of these with identical dimensions are NOT
    // interchangeable -- a render target is a place to draw, not a value -- so
    // `name` distinguishes them and folds into the key.
    struct RenderTargetTextureDesc
    {
        std::string             name;
        uint32_t                width  = 512;
        uint32_t                height = 512;
        TextureColorSpaceChoice color_space = TextureColorSpaceChoice::Auto;
    };

    class TextureAssetModule
    {
    public:
        TextureAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            TextureTable& table);

        TextureAsset create_from_file(const TextureFromFileDesc& desc);

        TextureAsset create_render_target(const RenderTargetTextureDesc& desc);

        TextureHandle get_texture(const TextureAsset& asset) const;

        const TextureData* get_texture_data(TextureHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger&             logger_;
        TextureTable&           table_;
    };
}
