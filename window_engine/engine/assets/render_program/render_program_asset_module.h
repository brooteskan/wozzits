#pragma once

// engine/assets/render_program/render_program_asset_module.h

#include <asset/types.h>
#include <engine/assets/render_program/render_program.h>
#include <gpu/gpu.h>

namespace wz::asset
{
    class AssetSystem;
}

namespace wz::engine::assets
{
    struct RenderProgramAsset
    {
        wz::asset::AssetKey key{};

        bool valid() const noexcept
        {
            return !(key == wz::asset::AssetKey{});
        }
    };

    class RenderProgramAssetModule
    {
    public:
        RenderProgramAssetModule(
            wz::asset::AssetSystem& system,
            RenderProgramTable& table);

        RenderProgramAsset create_builtin(BuiltinRenderProgramDesc desc);

        wz::asset::ResourceHandle get_render_program(
            RenderProgramAsset asset) const;

        const RenderProgramData* get_render_program_data(
            wz::asset::ResourceHandle handle) const;

        // Create and store the DX12 pipeline for an already-compiled program.
        // No-op (returns true) if the pipeline was already realized.
        // Returns false if the asset is unresolved or PSO creation fails.
        bool realize_pipeline(
            wz::gpu::Device&   device,
            RenderProgramAsset asset);

        RenderProgramTable& table();
        const RenderProgramTable& table() const;

    private:
        wz::asset::AssetSystem* system_ = nullptr;
        RenderProgramTable* table_ = nullptr;
    };
}