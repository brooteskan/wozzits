#pragma once

// engine/assets/texture/texture.h
//
// Runtime data types + table for the Texture asset: an image imported as a
// GPU-resident 2D texture (SRV) -- the foundational texel source overlays and,
// later, materials sample.
//
// An imported image is an rhi SURROGATE. The decoded RGBA8 pixels live in a
// resident wozzits-rhi Texture2D published under rhi_asset_identity(key,
// "texture") (see texture_compilers.cpp); THIS table holds only the CPU-side
// SHAPE a consumer needs to bind and interpret it -- dimensions plus the two
// semantics that are load-bearing above the primitive: colour space and usage.
// The pixels are never kept in CPU RAM (an image is large; a StarCatalog keeps
// its points, a Texture does not).
//
// Colour space is the single most important field: an overlay sprite is sRGB
// (display-referred), a normal map / mask / LUT is linear DATA. It is carried as
// metadata so consumers decode correctly; the ENFORCEMENT (an sRGB sampler or
// format) can evolve behind the stable handle without touching this layout.
// Residency is likewise a policy behind the handle -- seam 1 is resident-only.
// Keeping streaming/virtual/compressed as properties of ONE Texture type (a
// format here, a residency mode later), rather than sibling asset types, is the
// asset-type-vs-recipe rule applied to texels.

#include <asset/types.h>
#include <engine/assets/type_extensions.h>

#include <cstdint>
#include <vector>

namespace wz::engine::assets
{
    // sRGB (display-referred: colour / UI) vs Linear (data: normal / mask / LUT).
    // The RESOLVED runtime value -- an authoring "auto" is resolved from usage at
    // compile time and never stored here.
    enum class TextureColorSpace : uint8_t { Srgb, Linear };

    // What the texture is FOR. Seeds the default colour space (Color/UI -> sRGB,
    // Normal/Data -> Linear) and, later, the default sampler. Intent the consumer
    // reads, not a data layout.
    enum class TextureUsage : uint8_t { Color, Normal, Data, UI };

    // AUTHORING choice for colour space. Auto resolves from usage at compile time;
    // Srgb / Linear force it. The stored TextureColorSpace is always resolved. The
    // enumerator order IS the compiler's "color_space" Enum-param option order
    // (auto / srgb / linear), so static_cast<int> gives the authored index.
    enum class TextureColorSpaceChoice : uint8_t { Auto, Srgb, Linear };

    // GPU pixel layout. Seam 1 imports 8-bit RGBA; leaving room for single-channel
    // and block-compressed formats here (not new asset types) keeps the format
    // ladder a property of ONE Texture type.
    enum class TexturePixelFormat : uint8_t { RGBA8 };

    // Immutable CPU-side texture metadata. Pixels live in the resident rhi
    // Texture2D under rhi_asset_identity(key, "texture"); this is only what a bind
    // needs. A zero-dimension texture is invalid.
    struct TextureData
    {
        uint32_t           width       = 0;
        uint32_t           height      = 0;
        TexturePixelFormat format      = TexturePixelFormat::RGBA8;
        TextureColorSpace  color_space = TextureColorSpace::Srgb;
        TextureUsage       usage       = TextureUsage::Color;

        bool valid() const noexcept { return width > 0u && height > 0u; }
    };

    // Runtime owner of compiled texture metadata, keyed by ResourceHandle. Mirror
    // of StarCatalogTable / SkyGaussianTable: slot 0 is the invalid sentinel so a
    // default (id 0, epoch 0) handle never resolves; append-only; epoch starts 1.
    class TextureTable
    {
    public:
        TextureTable()
        {
            slots_.emplace_back();   // sentinel
            epochs_.push_back(0);
        }

        wz::asset::ResourceHandle add(TextureData data)
        {
            if (!data.valid()) {
                return {};
            }
            const uint32_t id = static_cast<uint32_t>(slots_.size());
            slots_.push_back(data);
            epochs_.push_back(1);
            return wz::asset::ResourceHandle{
                .id = id,
                .epoch = epochs_[id],
                .type = kAssetTypeTexture,
            };
        }

        const TextureData* get(wz::asset::ResourceHandle handle) const
        {
            if (!handle.valid()) return nullptr;
            if (handle.type != kAssetTypeTexture) return nullptr;
            if (handle.id >= slots_.size()) return nullptr;
            if (epochs_[handle.id] != handle.epoch) return nullptr;
            return &slots_[handle.id];
        }

        void destroy()
        {
            slots_.clear();
            epochs_.clear();
            slots_.emplace_back();
            epochs_.push_back(0);
        }

    private:
        std::vector<TextureData> slots_;
        std::vector<uint32_t>    epochs_;
    };
}
