// src/engine/assets/shader_asset_module.cpp

#include <engine/assets/shader_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/key_factories/hlsl_shader.h>

#include <gpu/shader.h>

#include <vector>

namespace wz::engine::assets
{

    ShaderAssetModule::ShaderAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger&             logger,
        FileCarrierAssetModule& files)
        : system_(system)
        , logger_(logger)
        , files_(files)
    {
    }

    ShaderPairAsset ShaderAssetModule::create_shader_pair(const ShaderPairDesc& desc)
    {
        ShaderPairAsset out{};

        // register_hlsl_shader_node inlined as a local lambda — called twice,
        // once for VS and once for PS, to avoid duplicating node-building logic.
        auto register_shader = [&](
            const wz::fs::Path&             path,
            const wz::gpu::HLSLCompileDesc& compile_desc
        ) -> wz::asset::AssetKey
        {
            const wz::asset::AssetKey file = files_.register_file_node(
                path, kHLSLFileSchema, wz::asset::AssetType::ShaderSource);

            if (file == wz::asset::AssetKey{})
                return {};

            const wz::asset::AssetKey key = make_hlsl_shader_key(
                file,
                static_cast<uint8_t>(compile_desc.stage),
                compile_desc.entry,
                compile_desc.target
            );

            wz::asset::AssetNode node;
            node.key     = key;
            node.type    = wz::asset::AssetType::Shader;
            node.schema  = kHLSLShaderSchema;
            node.stage   = wz::asset::AssetStage::Source;
            node.payload = std::vector<uint8_t>{};
            node.meta    = compile_desc;

            if (!system_.register_asset(std::move(node), { file })) {
                logger_.error("failed to register HLSL shader node");
                return {};
            }

            return key;
        };

        out.vertex_shader = register_shader(desc.vertex_path, {
            .stage                = wz::gpu::ShaderStage::Vertex,
            .entry                = desc.vertex_entry,
            .target               = desc.vertex_target,
            .primary_source_index = 0,
        });

        if (out.vertex_shader == wz::asset::AssetKey{}) {
            logger_.error("failed to register vertex shader: " + desc.name);
            return {};
        }

        out.pixel_shader = register_shader(desc.pixel_path, {
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

} // namespace wz::engine::assets
