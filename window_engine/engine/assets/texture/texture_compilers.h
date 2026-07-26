#pragma once

// engine/assets/texture/texture_compilers.h

#include <asset/compiler.h>
#include <asset/system.h>
#include <engine/assets/texture/texture.h>
#include <engine/assets/gpu_sparse_mesh/gpu_sparse_mesh_compilers.h>  // RhiResourceTracker
#include <logging/logger.h>

namespace wz::rhi
{
    class GpuResourceRegistry;
}

namespace wz::engine::assets::internal
{
    // Registrar for the Texture-from-file compiler. Reads a RawFile dependency
    // (the image bytes), decodes it to RGBA8 (stb_image), stores the CPU-side
    // metadata in the TextureTable, and -- when a shared rhi registry is present
    // -- publishes the pixels as a resident Texture2D (variant "texture") under
    // rhi_asset_identity so the generic render-binding path can bind it. Null
    // gpu_resources (a device-only library) skips rhi residency; the metadata is
    // still valid. Mirrors register_star_catalog_compilers.
    void register_texture_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        TextureTable& table,
        wz::rhi::GpuResourceRegistry* gpu_resources = nullptr,
        RhiResourceTracker rhi_resource_tracker = {},
        // Needed only by the composite-material recipe (#285), to recover its
        // PORT-ORDERED dep keys -- optional layer ports shift dep positions, so
        // the dep node order cannot say which texture is which layer.
        const wz::asset::AssetSystem* asset_system = nullptr);
}
