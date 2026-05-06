// engine/assets/engine_asset_library.cpp

#include <engine/assets/engine_asset_library.h>

#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/key_factories/file_carrier.h>
#include <engine/assets/key_factories/hlsl_shader.h>
#include <engine/assets/key_factories/scalar_field.h>

#include <engine/assets/shader/shader_types.h>
#include <gpu/shader.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <vector>
#include <any>

namespace wz::engine::assets
{
    namespace
    {

        // ── FileSourceDesc ────────────────────────────────────────────────────────
        //
        // Stored in AssetNode::meta for file carrier nodes.
        // The file bytes are read lazily at compile time, not at registration time.

        struct FileSourceDesc
        {
            wz::fs::Path full_path;
            std::string  canonical_path;
        };


        // ── compile_failed_node ───────────────────────────────────────────────────
        //
        // Returns the node unchanged (still Source stage) so resolve() sees
        // stage != Compiled and reports ResolveError::CompileFailed.

        static wz::asset::AssetNode compile_failed_node(
            const wz::asset::AssetNode& input)
        {
            return input;
        }


        // ── make_engine_compiler_registry ─────────────────────────────────────────

        wz::asset::CompilerRegistry make_engine_compiler_registry(
            wz::gpu::Device& device,
            wz::Logger& logger,
            ScalarFieldTable& scalar_field_table)
        {
            wz::asset::CompilerRegistry registry;


            // ── Raw file carrier compiler ─────────────────────────────────────────
            //
            // Dispatches on kRawFileSchema. Reads the file from FileSourceDesc
            // metadata and returns a compiled node carrying the raw bytes.
            // Used as the upstream dependency for the scalar field compiler.

            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kRawFileSchema,
                .output_type = kAssetTypeRawFile,
                .compile = [&logger](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    const auto* file =
                        std::any_cast<FileSourceDesc>(&input.meta);

                    if (!file) {
                        logger.error("raw file carrier missing FileSourceDesc");
                        return compile_failed_node(input);
                    }

                    auto file_result = wz::fs::read_file(file->full_path);
                    if (!file_result) {
                        logger.error("failed to read file: " + file->full_path);
                        return compile_failed_node(input);
                    }

                    wz::asset::AssetNode out = input;
                    out.stage = wz::asset::AssetStage::Compiled;
                    out.payload = std::move(file_result.value);
                    return out;
                }
                });


            // ── HLSL file carrier compiler ────────────────────────────────────────
            //
            // Separate from the raw file carrier so HLSL files are not confused
            // with raw binary assets at the schema dispatch level.

            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kHLSLFileSchema,
                .output_type = wz::asset::AssetType::ShaderSource,
                .compile = [&logger](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    const auto* file =
                        std::any_cast<FileSourceDesc>(&input.meta);

                    if (!file) {
                        logger.error("HLSL file carrier missing FileSourceDesc");
                        return compile_failed_node(input);
                    }

                    auto file_result = wz::fs::read_file(file->full_path);
                    if (!file_result) {
                        logger.error("failed to read file: " + file->full_path);
                        return compile_failed_node(input);
                    }

                    wz::asset::AssetNode out = input;
                    out.stage = wz::asset::AssetStage::Compiled;
                    out.payload = std::move(file_result.value);
                    return out;
                }
                });


            // ── HLSL shader compiler ──────────────────────────────────────────────

            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kHLSLShaderSchema,
                .output_type = wz::asset::AssetType::Shader,
                .compile = [&logger, &device](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
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

                    for (const wz::asset::AssetNode& dep : dep_nodes) {
                        const auto* bytes =
                            std::get_if<std::vector<uint8_t>>(&dep.payload);

                        if (!bytes) {
                            logger.error("file dep node has no byte payload");
                            return compile_failed_node(input);
                        }

                        sources.push_back({ bytes->data(), bytes->size() });
                    }

                    wz::gpu::GPUHandle gpu_handle =
                        wz::gpu::compile_hlsl(device, sources, *desc);

                    if (!gpu_handle.valid()) {
                        logger.error("gpu.compile_hlsl failed");
                        return compile_failed_node(input);
                    }

                    wz::asset::AssetNode out = input;
                    out.stage = wz::asset::AssetStage::Compiled;
                    out.payload = wz::asset::ResourceHandle{
                        .id = gpu_handle.id,
                        .epoch = gpu_handle.epoch,
                        .type = wz::asset::AssetType::Shader,
                    };

                    return out;
                }
                });


            // ── Scalar field compiler ─────────────────────────────────────────────
            //
            // Dispatches on kScalarFieldFromRawF32Schema.
            // Expects exactly one dependency: a kRawFileSchema node whose compiled
            // payload is a std::vector<uint8_t> of raw float32 bytes.
            //
            // On success: stores ScalarFieldData in scalar_field_table and returns
            // a compiled node whose payload is the corresponding ResourceHandle.
            //
            // On failure: returns the unchanged source node (compile_failed_node),
            // which causes resolve() to report ResolveError::CompileFailed.

            registry.register_compiler(wz::asset::AssetCompiler{
                .input_schema = kScalarFieldFromRawF32Schema,
                .output_type = kAssetTypeScalarField,
                .compile = [&logger, &scalar_field_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
                {
                    // ── 1. Validate metadata ──────────────────────────────────────

                    const auto* desc =
                        std::any_cast<ScalarFieldCompileDesc>(&input.meta);

                    if (!desc) {
                        logger.error("scalar field node missing ScalarFieldCompileDesc");
                        return compile_failed_node(input);
                    }

                    if (desc->width == 0 || desc->height == 0 || desc->depth == 0) {
                        logger.error("scalar field has zero dimension ("
                            + std::to_string(desc->width) + "x"
                            + std::to_string(desc->height) + "x"
                            + std::to_string(desc->depth) + ")");
                        return compile_failed_node(input);
                    }

                    // ── 2. Validate dependency ────────────────────────────────────

                    if (dep_nodes.empty()) {
                        logger.error("scalar field node has no file dependency");
                        return compile_failed_node(input);
                    }

                    const auto* bytes =
                        std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

                    if (!bytes) {
                        logger.error("scalar field dep node has no byte payload");
                        return compile_failed_node(input);
                    }

                    // ── 3. Validate byte count ────────────────────────────────────

                    const uint32_t count =
                        desc->width * desc->height * desc->depth;

                    const uint32_t expected_bytes =
                        count * static_cast<uint32_t>(sizeof(float));

                    if (bytes->size() != expected_bytes) {
                        logger.error("scalar field byte count mismatch: expected "
                            + std::to_string(expected_bytes)
                            + " bytes ("
                            + std::to_string(desc->width) + "x"
                            + std::to_string(desc->height) + "x"
                            + std::to_string(desc->depth) + "xf32"
                            + "), got "
                            + std::to_string(bytes->size()));
                        return compile_failed_node(input);
                    }

                    // ── 4. Reinterpret bytes as float32 values ────────────────────

                    std::vector<float> values(count);
                    std::memcpy(values.data(), bytes->data(), expected_bytes);

                    // ── 5. Validate values; compute min/max ───────────────────────
                    //
                    // NaN and infinity are rejected in V1. This keeps debug
                    // visualisation predictable and makes min/max well-defined.

                    float min_val = std::numeric_limits<float>::max();
                    float max_val = std::numeric_limits<float>::lowest();

                    for (uint32_t i = 0; i < count; ++i) {
                        const float v = values[i];

                        if (std::isnan(v)) {
                            logger.error("scalar field contains NaN at sample index "
                                + std::to_string(i));
                            return compile_failed_node(input);
                        }

                        if (std::isinf(v)) {
                            logger.error("scalar field contains infinity at sample index "
                                + std::to_string(i));
                            return compile_failed_node(input);
                        }

                        if (v < min_val) min_val = v;
                        if (v > max_val) max_val = v;
                    }

                    // ── 6. Store in ScalarFieldTable ──────────────────────────────

                    ScalarFieldData data;
                    data.width = desc->width;
                    data.height = desc->height;
                    data.depth = desc->depth;
                    data.format = desc->format;
                    data.domain_kind = desc->domain_kind;
                    data.min_value = min_val;
                    data.max_value = max_val;
                    data.values = std::move(values);

                    wz::asset::ResourceHandle handle =
                        scalar_field_table.add(std::move(data));

                    // ── 7. Return compiled node ───────────────────────────────────

                    wz::asset::AssetNode out = input;
                    out.stage = wz::asset::AssetStage::Compiled;
                    out.payload = handle;
                    return out;
                }
                });

            return registry;
        }

    } // anonymous namespace


    // ─── EngineAssetLibrary ───────────────────────────────────────────────────────

    EngineAssetLibrary::EngineAssetLibrary(
        wz::gpu::Device& device,
        wz::Logger& logger,
        wz::fs::Path     resource_root)
        : device_(device)
        , logger_(logger)
        , resource_root_(std::move(resource_root))
        , scalar_fields_{}
        , system_(make_engine_compiler_registry(device, logger, scalar_fields_))
        // scalar_fields_ is declared before system_ in the class, so it is
        // fully constructed by the time make_engine_compiler_registry runs.
    {
    }


    // ─── Private helpers ──────────────────────────────────────────────────────────

    wz::asset::AssetKey EngineAssetLibrary::register_file_node(
        const wz::fs::Path& relative_path,
        wz::asset::SchemaID  schema,
        wz::asset::AssetType type)
    {
        const std::string canonical =
            detail::canonical_asset_path(relative_path);

        const wz::asset::AssetKey key = make_file_key(canonical, schema);

        const wz::fs::Path full_path =
            wz::fs::join(resource_root_, relative_path);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = type;
        node.schema = schema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = FileSourceDesc{
            .full_path = full_path,
            .canonical_path = canonical,
        };

        if (!system_.register_asset(std::move(node))) {
            logger_.error("failed to register file node: " + canonical);
            return {};
        }

        return key;
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

    wz::asset::AssetKey EngineAssetLibrary::register_scalar_field_node(
        wz::asset::AssetKey          source_file,
        const ScalarFieldCompileDesc& desc)
    {
        const wz::asset::AssetKey key = make_scalar_field_key(
            source_file,
            desc.width,
            desc.height,
            desc.depth,
            static_cast<uint8_t>(desc.format)
        );

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeScalarField;
        node.schema = kScalarFieldFromRawF32Schema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = desc;

        if (!system_.register_asset(
            std::move(node),
            std::vector<wz::asset::AssetKey>{ source_file }))
        {
            logger_.error("failed to register scalar field node");
            return {};
        }

        return key;
    }


    // ─── Public API ───────────────────────────────────────────────────────────────

    ShaderPairAsset EngineAssetLibrary::create_shader_pair(
        const ShaderPairDesc& desc)
    {
        ShaderPairAsset out{};

        const wz::asset::AssetKey vs_file = register_file_node(
            desc.vertex_path,
            kHLSLFileSchema,
            wz::asset::AssetType::ShaderSource
        );

        if (vs_file == wz::asset::AssetKey{}) {
            logger_.error("failed to register vertex shader file: " + desc.vertex_path);
            return out;
        }

        const wz::asset::AssetKey ps_file = register_file_node(
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

    ScalarFieldAsset EngineAssetLibrary::create_scalar_field(
        const ScalarFieldFileDesc& desc)
    {
        ScalarFieldAsset out{};

        // Register the raw file carrier node. This is a generic file-bytes node;
        // the scalar field compiler will interpret those bytes.
        const wz::asset::AssetKey file_key = register_file_node(
            desc.path,
            kRawFileSchema,
            kAssetTypeRawFile
        );

        if (file_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register scalar field source file: " + desc.path);
            return out;
        }

        // Register the scalar field recipe node, depending on the file node.
        const ScalarFieldCompileDesc compile_desc{
            .width = desc.width,
            .height = desc.height,
            .depth = desc.depth,
            .format = desc.format,
            .domain_kind = desc.domain_kind,
        };

        const wz::asset::AssetKey field_key =
            register_scalar_field_node(file_key, compile_desc);

        if (field_key == wz::asset::AssetKey{}) {
            logger_.error("failed to register scalar field node: " + desc.name);
            return out;
        }

        out.output = field_key;
        return out;
    }

    bool EngineAssetLibrary::commit()
    {
        if (!system_.commit()) {
            logger_.error("asset graph rejected — cycle or missing dependency");
            return false;
        }

        return true;
    }

    uint32_t EngineAssetLibrary::resolve_all()
    {
        std::vector<std::pair<wz::asset::AssetKey, wz::asset::ResolveError>> errors;

        const uint32_t count = system_.resolve_all(&errors);

        if (!errors.empty()) {
            logger_.error("asset resolve_all failed");
        }

        return count;
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

    ScalarFieldHandle EngineAssetLibrary::get_scalar_field(
        const ScalarFieldAsset& asset) const
    {
        ScalarFieldHandle out{};

        if (!asset.valid())
            return out;

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("scalar field handle not found");

        return out;
    }

    const ScalarFieldData* EngineAssetLibrary::get_scalar_field_data(
        ScalarFieldHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return scalar_fields_.get(handle.handle);
    }

} // namespace wz::engine::assets
