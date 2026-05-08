#include <engine/assets/test/triangle_shader_assets.h>
#include <engine/assets/shader/shader_types.h>
#include <asset/types.h>
#include <file/filesystem.h>
#include <gpu/dx12/dx12.h>
#include <gpu/shader.h>

#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/key_factories/hlsl_shader.h>
#include <engine/assets/schema_ids.h>

using namespace wz::asset;
using namespace wz::fs;

namespace wz::engine::assets::test
{

    static wz::fs::FileResult<AssetNode> make_file_node(
        const Path& path,
        AssetKey key,
        SchemaID schema,
        AssetType type)
    {
        auto file_result = read_file(path);
        if (!file_result)
            return { {}, file_result.error };

        Buffer& bytes = file_result.value;

        AssetNode node;
        node.key = key;
        node.type = type;
        node.schema = schema;
        node.stage = AssetStage::Source;
        node.payload = std::move(bytes);

        return { std::move(node), wz::fs::FileError::None };
    }

    // ─── compile_failed_node ──────────────────────────────────────────────────────
    // Returns the node unchanged (still Source stage) so resolve() sees
    // stage != Compiled and produces ResolveError::CompileFailed.

    static AssetNode compile_failed_node(const AssetNode& input) { return input; }

    // ─── Triangle test resources ─────────────────────────────────────────────────
    // Owns the shader handles needed to initialize the temporary triangle test
    // render context. The handles refer to blobs owned by the DX12 shader table.

    CompilerRegistry make_triangle_test_compiler_registry(
        wz::gpu::Device& device,
        wz::Logger& logger)
    {
        CompilerRegistry registry;

        registry.register_compiler(AssetCompiler{
            .input_schema = wz::engine::assets::kHLSLFileSchema,
            .output_type = AssetType::ShaderSource,
            .compile = [](const AssetNode& input, auto, auto) -> AssetNode {
                AssetNode out = input;
                out.stage = AssetStage::Compiled;
                return out;
            }
            });

        registry.register_compiler(AssetCompiler{
            .input_schema = wz::engine::assets::kHLSLShaderSchema,
            .output_type = AssetType::Shader,
            .compile = [&logger, &device](
                const AssetNode& input,
                std::span<const AssetNode> dep_nodes,
                std::span<const ResourceHandle>) -> AssetNode
            {
                const auto* desc =
                    std::any_cast<wz::gpu::HLSLCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("shader node missing HLSLCompileDesc");
                    return compile_failed_node(input);
                }

                if (dep_nodes.empty()) {
                    logger.error("shader node has no source dependencies");
                    return compile_failed_node(input);
                }

                if (desc->primary_source_index >= dep_nodes.size()) {
                    logger.error("shader primary_source_index out of range");
                    return compile_failed_node(input);
                }

                std::vector<std::span<const uint8_t>> sources;
                sources.reserve(dep_nodes.size());

                for (const AssetNode& dep : dep_nodes) {
                    const auto* bytes =
                        std::get_if<std::vector<uint8_t>>(&dep.payload);

                    if (!bytes) {
                        logger.error("file dep node has no byte payload");
                        return compile_failed_node(input);
                    }

                    sources.push_back({ bytes->data(), bytes->size() });

                    logger.info("  source bytes available: "
                        + std::to_string(bytes->size()) + " bytes");
                }

                wz::gpu::GPUHandle gpu_handle =
                    wz::gpu::compile_hlsl(device, sources, *desc);

                if (!gpu_handle.valid()) {
                    logger.error("gpu.compile_hlsl failed");
                    return compile_failed_node(input);
                }

                logger.info("shader link: entry=" + desc->entry
                    + " target=" + desc->target
                    + " sources=" + std::to_string(sources.size()));

                AssetNode out = input;
                out.stage = AssetStage::Compiled;
                out.payload = ResourceHandle{
                    .id = gpu_handle.id,
                    .epoch = gpu_handle.epoch,
                    .type = AssetType::Shader
                };
                return out;
            }
            });

        return registry;
    }

    static bool register_triangle_shader_assets(
        AssetSystem& asset_sys,
        const Path& resource_root,
        wz::Logger& logger)
    {
        static constexpr std::string_view kTriangleVSRel =
            "shaders/triangle/triangle_vs.hlsl";

        static constexpr std::string_view kTrianglePSRel =
            "shaders/triangle/triangle_ps.hlsl";

        const Path vs_path =
            wz::fs::join(resource_root, "shaders\\triangle\\triangle_vs.hlsl");

        const Path ps_path =
            wz::fs::join(resource_root, "shaders\\triangle\\triangle_ps.hlsl");

        AssetKey vs_file_key =
            wz::engine::assets::make_file_key(
                kTriangleVSRel,
                wz::engine::assets::kHLSLFileSchema
            );

        AssetKey ps_file_key =
            wz::engine::assets::make_file_key(
                kTrianglePSRel,
                wz::engine::assets::kHLSLFileSchema
            );

        auto vs_file = make_file_node(
            vs_path,
            vs_file_key,
            wz::engine::assets::kHLSLFileSchema,
            AssetType::ShaderSource
        );

        if (!vs_file) {
            logger.error("failed to read vertex shader: " + vs_path);
            return false;
        }

        auto ps_file = make_file_node(
            ps_path,
            ps_file_key,
            wz::engine::assets::kHLSLFileSchema,
            AssetType::ShaderSource
        );

        if (!ps_file) {
            logger.error("failed to read pixel shader: " + ps_path);
            return false;
        }

        if (!asset_sys.register_asset(std::move(vs_file.value))) {
            logger.error("failed to register vertex shader source");
            return false;
        }

        if (!asset_sys.register_asset(std::move(ps_file.value))) {
            logger.error("failed to register pixel shader source");
            return false;
        }

        AssetKey vs_shader_key =
            wz::engine::assets::make_hlsl_shader_key(
                vs_file_key,
                static_cast<uint8_t>(wz::gpu::ShaderStage::Vertex),
                "main",
                "vs_5_0"
            );

        AssetNode vs_shader_node;
        vs_shader_node.key = vs_shader_key;
        vs_shader_node.type = AssetType::Shader;
        vs_shader_node.schema = wz::engine::assets::kHLSLShaderSchema;
        vs_shader_node.stage = AssetStage::Source;
        vs_shader_node.payload = std::vector<uint8_t>{};
        vs_shader_node.meta = wz::gpu::HLSLCompileDesc{
            .stage = wz::gpu::ShaderStage::Vertex,
            .entry = "main",
            .target = "vs_5_0",
            .primary_source_index = 0,
        };

        if (!asset_sys.register_asset(
            std::move(vs_shader_node),
            std::vector<AssetKey>{ vs_file_key }))
        {
            logger.error("failed to register vertex shader asset");
            return false;
        }

        AssetKey ps_shader_key =
            wz::engine::assets::make_hlsl_shader_key(
                ps_file_key,
                static_cast<uint8_t>(wz::gpu::ShaderStage::Pixel),
                "main",
                "ps_5_0"
            );

        AssetNode ps_shader_node;
        ps_shader_node.key = ps_shader_key;
        ps_shader_node.type = AssetType::Shader;
        ps_shader_node.schema = wz::engine::assets::kHLSLShaderSchema;
        ps_shader_node.stage = AssetStage::Source;
        ps_shader_node.payload = std::vector<uint8_t>{};
        ps_shader_node.meta = wz::gpu::HLSLCompileDesc{
            .stage = wz::gpu::ShaderStage::Pixel,
            .entry = "main",
            .target = "ps_5_0",
            .primary_source_index = 0,
        };

        if (!asset_sys.register_asset(
            std::move(ps_shader_node),
            std::vector<AssetKey>{ ps_file_key }))
        {
            logger.error("failed to register pixel shader asset");
            return false;
        }

        return true;
    }

    static TriangleTestResources load_triangle_test_resources(
        const AssetSystem& asset_sys,
        wz::Logger& logger)
    {
        TriangleTestResources out{};

        auto shaders = asset_sys.query(
            AssetType::Shader,
            wz::engine::assets::kHLSLShaderSchema
        );

        if (shaders.size() != 2)
        {
            logger.error("expected exactly 2 compiled HLSL shaders, got "
                + std::to_string(shaders.size()));
            return out;
        }

        for (const auto& shader : shaders)
        {
            const auto* desc =
                std::any_cast<wz::gpu::HLSLCompileDesc>(&shader.node->meta);

            if (!desc)
            {
                logger.error("compiled shader missing HLSLCompileDesc metadata");
                continue;
            }

            if (desc->stage == wz::gpu::ShaderStage::Vertex)
                out.vertex_shader = shader.handle;

            if (desc->stage == wz::gpu::ShaderStage::Pixel)
                out.pixel_shader = shader.handle;
        }

        if (!out.vertex_shader.valid())
            logger.error("triangle test vertex shader handle was not found");

        if (!out.pixel_shader.valid())
            logger.error("triangle test pixel shader handle was not found");

        return out;
    }

    TriangleTestResources initialize_triangle_test_assets(
        AssetSystem& asset_sys,
        const Path& resource_root,
        wz::Logger& logger)
    {

        TriangleTestResources out{};

        if (!register_triangle_shader_assets(asset_sys, resource_root, logger))
            return out;

        if (!asset_sys.commit()) {
            logger.error("asset graph rejected — cycle or missing dependency");
            return out;
        }

        logger.info("asset graph committed");

        std::vector<std::pair<AssetKey, ResolveError>> errors;
        const uint32_t resolved_count = asset_sys.resolve_all(&errors);

        logger.info("resolved asset count: " + std::to_string(resolved_count));

        if (!errors.empty()) {
            logger.error("asset resolve failed");
            return out;
        }

        return load_triangle_test_resources(asset_sys, logger);
    }

}