// engine/assets/engine_asset_library_files.cpp

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/key_factories/hlsl_shader.h>

namespace wz::engine::assets
{

    ShaderPairAsset EngineAssetLibrary::create_shader_pair(
        const ShaderPairDesc& desc)
    {
        ShaderPairAsset out{};

        const wz::asset::AssetKey vs_file = files_.register_file_node(
            desc.vertex_path,
            kHLSLFileSchema,
            wz::asset::AssetType::ShaderSource
        );

        if (vs_file == wz::asset::AssetKey{}) {
            logger_.error("failed to register vertex shader file: " + desc.vertex_path);
            return out;
        }

        const wz::asset::AssetKey ps_file = files_.register_file_node(
            desc.pixel_path,
            kHLSLFileSchema,
            wz::asset::AssetType::ShaderSource
        );

        if (ps_file == wz::asset::AssetKey{}) {
            logger_.error("failed to register pixel shader file: " + desc.pixel_path);
            return out;
        }

        wz::gpu::HLSLCompileDesc vs_desc{
            .stage = wz::gpu::ShaderStage::Vertex,
            .entry = desc.vertex_entry,
            .target = desc.vertex_target,
            .primary_source_index = 0,
        };

        wz::gpu::HLSLCompileDesc ps_desc{
            .stage = wz::gpu::ShaderStage::Pixel,
            .entry = desc.pixel_entry,
            .target = desc.pixel_target,
            .primary_source_index = 0,
        };

        out.vertex_shader =
            register_hlsl_shader_node(desc.name + ":vs", vs_file, vs_desc);

        out.pixel_shader =
            register_hlsl_shader_node(desc.name + ":ps", ps_file, ps_desc);

        return out;
    }

    ShaderPairHandles EngineAssetLibrary::get_shader_pair(
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

    wz::asset::AssetKey EngineAssetLibrary::register_hlsl_shader_node(
        std::string_view            name,
        wz::asset::AssetKey         source_file,
        const wz::gpu::HLSLCompileDesc& desc)
    {
        const wz::asset::AssetKey key = make_hlsl_shader_key(
            source_file,
            static_cast<uint8_t>(desc.stage),
            desc.entry,
            desc.target
        );

        wz::asset::AssetNode node;
        node.key = key;
        node.type = wz::asset::AssetType::Shader;
        node.schema = kHLSLShaderSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = desc;

        if (!system_.register_asset(
            std::move(node),
            std::vector<wz::asset::AssetKey>{ source_file }))
        {
            logger_.error("failed to register HLSL shader node");
            return {};
        }

        return key;
    }
}