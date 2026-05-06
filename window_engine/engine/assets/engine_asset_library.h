#pragma once

// engine/assets/engine_asset_library.h

#include <asset/system.h>
#include <asset/compiler.h>
#include <asset/types.h>

#include <file/filesystem.h>

#include <gpu/gpu.h>
#include <gpu/gpu_types.h>
#include <gpu/shader.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    struct ShaderPairDesc
    {
        std::string name;

        wz::fs::Path vertex_path;
        wz::fs::Path pixel_path;

        std::string vertex_entry = "main";
        std::string pixel_entry = "main";

        std::string vertex_target = "vs_5_0";
        std::string pixel_target = "ps_5_0";
    };

    struct ShaderPairAsset
    {
        wz::asset::AssetKey vertex_shader{};
        wz::asset::AssetKey pixel_shader{};

        bool valid() const noexcept
        {
            return vertex_shader != wz::asset::AssetKey{} &&
                pixel_shader != wz::asset::AssetKey{};
        }
    };

    struct ShaderPairHandles
    {
        wz::gpu::GPUHandle vertex{};
        wz::gpu::GPUHandle pixel{};

        bool valid() const noexcept
        {
            return vertex.valid() && pixel.valid();
        }
    };

    class EngineAssetLibrary
    {
    public:
        EngineAssetLibrary(
            wz::gpu::Device& device,
            wz::Logger& logger,
            wz::fs::Path resource_root
        );

        EngineAssetLibrary(const EngineAssetLibrary&) = delete;
        EngineAssetLibrary& operator=(const EngineAssetLibrary&) = delete;

        EngineAssetLibrary(EngineAssetLibrary&&) = delete;
        EngineAssetLibrary& operator=(EngineAssetLibrary&&) = delete;

        ShaderPairAsset create_shader_pair(const ShaderPairDesc& desc);

        bool commit();
        uint32_t resolve_all();

        ShaderPairHandles get_shader_pair(const ShaderPairAsset& asset) const;

        wz::asset::AssetSystem& system() { return system_; }
        const wz::asset::AssetSystem& system() const { return system_; }

    private:
        wz::asset::AssetKey register_file_node(
            const wz::fs::Path& relative_path,
            wz::asset::SchemaID schema,
            wz::asset::AssetType type
        );

        wz::asset::AssetKey register_hlsl_shader_node(
            std::string_view name,
            wz::asset::AssetKey source_file,
            const wz::gpu::HLSLCompileDesc& desc
        );

    private:
        wz::gpu::Device& device_;
        wz::Logger& logger_;
        wz::fs::Path resource_root_;

        wz::asset::AssetSystem system_;
    };
}