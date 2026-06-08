// src/engine/assets/shader_asset_module.cpp

#include <engine/assets/shader_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/key_factories/hlsl_shader.h>

#include <gpu/shader.h>

#include <vector>

namespace wz::engine::assets
{
    namespace
    {
        wz::asset::AssetKey register_hlsl_shader_node(
            wz::asset::AssetSystem& system,
            FileCarrierAssetModule& files,
            const wz::fs::Path& path,
            const wz::gpu::HLSLCompileDesc& compile_desc)
        {
            const wz::asset::AssetKey file = files.register_file_node(
                path, kHLSLFileSchema, wz::asset::AssetType::ShaderSource);

            if (file == wz::asset::AssetKey{})
                return {};

            const wz::asset::AssetKey key = make_hlsl_shader_key(
                file,
                static_cast<uint8_t>(compile_desc.stage),
                compile_desc.entry,
                compile_desc.target);

            wz::asset::AssetNode node;
            node.key     = key;
            node.type    = wz::asset::AssetType::Shader;
            node.schema  = kHLSLShaderSchema;
            node.stage   = wz::asset::AssetStage::Source;
            node.payload = std::vector<uint8_t>{};
            node.meta    = compile_desc;

            if (!system.register_asset(std::move(node), { file })) {
                // The key is a pure function of (file, stage, entry, target),
                // so duplicate registration means the same compile output.
                return key;
            }

            return key;
        }
    }

    ShaderAssetModule::ShaderAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        FileCarrierAssetModule& files)
        : system_(system)
        , logger_(logger)
        , files_(files)
    {
    }

    ShaderPairAsset ShaderAssetModule::create_shader_pair(
        const ShaderPairDesc& desc)
    {
        ShaderPairAsset out{};

        out.vertex_shader = register_hlsl_shader_node(
            system_,
            files_,
            desc.vertex_path,
            {
                .stage                = wz::gpu::ShaderStage::Vertex,
                .entry                = desc.vertex_entry,
                .target               = desc.vertex_target,
                .primary_source_index = 0,
            });

        if (out.vertex_shader == wz::asset::AssetKey{}) {
            logger_.error("failed to register vertex shader: " + desc.name);
            return {};
        }

        out.pixel_shader = register_hlsl_shader_node(
            system_,
            files_,
            desc.pixel_path,
            {
                .stage                = wz::gpu::ShaderStage::Pixel,
                .entry                = desc.pixel_entry,
                .target               = desc.pixel_target,
                .primary_source_index = 0,
            });

        if (out.pixel_shader == wz::asset::AssetKey{}) {
            logger_.error("failed to register pixel shader: " + desc.name);
            return {};
        }

        return out;
    }

    ShaderPairHandles ShaderAssetModule::get_shader_pair(
        const ShaderPairAsset& asset) const
    {
        ShaderPairHandles out{};

        if (!asset.valid())
            return out;

        if (const auto* vs = system_.find_compiled(asset.vertex_shader))
            out.vertex = vs->handle;

        if (const auto* ps = system_.find_compiled(asset.pixel_shader))
            out.pixel = ps->handle;

        if (!out.vertex.valid())
            logger_.error("vertex shader handle not found");

        if (!out.pixel.valid())
            logger_.error("pixel shader handle not found");

        return out;
    }

    ComputeShaderAsset ShaderAssetModule::create_compute_shader(
        const ComputeShaderDesc& desc)
    {
        if (desc.path.empty() || desc.entry.empty() || desc.target.empty()) {
            logger_.error("compute shader desc is incomplete: " + desc.name);
            return {};
        }

        const wz::asset::AssetKey key = register_hlsl_shader_node(
            system_,
            files_,
            desc.path,
            {
                .stage                = wz::gpu::ShaderStage::Compute,
                .entry                = desc.entry,
                .target               = desc.target,
                .primary_source_index = 0,
            });

        if (key == wz::asset::AssetKey{}) {
            logger_.error("failed to register compute shader: " + desc.name);
            return {};
        }

        return ComputeShaderAsset{ .shader = key };
    }

    ComputeShaderHandle ShaderAssetModule::get_compute_shader(
        const ComputeShaderAsset& asset) const
    {
        ComputeShaderHandle out{};

        if (!asset.valid())
            return out;

        if (const auto* cs = system_.find_compiled(asset.shader))
            out.shader = cs->handle;

        if (!out.shader.valid())
            logger_.error("compute shader handle not found");

        return out;
    }

} // namespace wz::engine::assets
