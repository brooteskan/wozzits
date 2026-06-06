// src/engine/assets/mesh/mesh_compilers.cpp

#include <engine/assets/mesh/mesh_compilers.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/mesh/procedural_mesh.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/mesh_processing/mesh_processing.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kGLBMeshDiskCacheMagic = 0x4d435a57u;
        constexpr uint32_t kGLBMeshDiskCacheVersion = 1u;

        template<typename T>
        void append_scalar(std::vector<uint8_t>& out, const T& value)
        {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }

        template<typename T>
        bool read_scalar(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            T& out)
        {
            if (offset + sizeof(T) > bytes.size()) {
                return false;
            }
            std::memcpy(&out, bytes.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        void append_raw_bytes(
            std::vector<uint8_t>& out,
            const void* data,
            size_t byte_count)
        {
            if (byte_count == 0u) {
                return;
            }
            const auto* first = static_cast<const uint8_t*>(data);
            out.insert(out.end(), first, first + byte_count);
        }

        bool read_raw_bytes(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            void* out,
            size_t byte_count)
        {
            if (byte_count == 0u) {
                return true;
            }
            if (offset + byte_count > bytes.size()) {
                return false;
            }
            std::memcpy(out, bytes.data() + offset, byte_count);
            offset += byte_count;
            return true;
        }

        void append_asset_key(
            std::vector<uint8_t>& out,
            const wz::asset::AssetKey& key)
        {
            append_scalar(out, key.content_hash.lo);
            append_scalar(out, key.content_hash.hi);
            append_scalar(out, key.schema_hash.lo);
            append_scalar(out, key.schema_hash.hi);
            append_scalar(out, key.compiler_hash.lo);
            append_scalar(out, key.compiler_hash.hi);
            append_scalar(out, key.deps_hash.lo);
            append_scalar(out, key.deps_hash.hi);
        }

        bool read_asset_key(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            wz::asset::AssetKey& key)
        {
            return read_scalar(bytes, offset, key.content_hash.lo)
                && read_scalar(bytes, offset, key.content_hash.hi)
                && read_scalar(bytes, offset, key.schema_hash.lo)
                && read_scalar(bytes, offset, key.schema_hash.hi)
                && read_scalar(bytes, offset, key.compiler_hash.lo)
                && read_scalar(bytes, offset, key.compiler_hash.hi)
                && read_scalar(bytes, offset, key.deps_hash.lo)
                && read_scalar(bytes, offset, key.deps_hash.hi);
        }

        uint64_t mix_cache_key_word(uint64_t state, uint64_t word)
        {
            state ^= word + 0x9e3779b97f4a7c15ull + (state << 6) + (state >> 2);
            state ^= state >> 30;
            state *= 0xbf58476d1ce4e5b9ull;
            state ^= state >> 27;
            state *= 0x94d049bb133111ebull;
            return state ^ (state >> 31);
        }

        std::string short_asset_key_hex(const wz::asset::AssetKey& key)
        {
            const uint64_t words[]{
                key.content_hash.lo, key.content_hash.hi,
                key.schema_hash.lo, key.schema_hash.hi,
                key.compiler_hash.lo, key.compiler_hash.hi,
                key.deps_hash.lo, key.deps_hash.hi,
            };

            uint64_t lo = 0x452821e638d01377ull;
            uint64_t hi = 0xbe5466cf34e90c6cull;
            for (uint64_t word : words) {
                lo = mix_cache_key_word(lo, word);
                hi = mix_cache_key_word(hi, word ^ lo);
            }

            std::ostringstream out;
            out << std::hex << std::setfill('0')
                << std::setw(16) << lo
                << std::setw(16) << hi;
            return out.str();
        }

        wz::fs::Path glb_mesh_cache_directory(
            const EngineAssetCacheSettings& cache)
        {
            return wz::fs::join(wz::fs::join(cache.root, "assets"), "glb_mesh");
        }

        wz::fs::Path glb_mesh_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return wz::fs::join(
                glb_mesh_cache_directory(cache),
                short_asset_key_hex(key) + ".bin");
        }

        bool read_vector_count(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            uint64_t& count,
            uint64_t min_bytes_per_entry)
        {
            if (!read_scalar(bytes, offset, count)) {
                return false;
            }
            const uint64_t remaining =
                static_cast<uint64_t>(bytes.size() - offset);
            return min_bytes_per_entry == 0u
                || count <= remaining / min_bytes_per_entry;
        }

        std::vector<uint8_t> serialize_mesh_asset(
            const wz::asset::AssetKey& key,
            const MeshData& mesh)
        {
            std::vector<uint8_t> out;
            out.reserve(
                128u
                + mesh.vertices.size() * sizeof(MeshVertex)
                + mesh.indices.size() * sizeof(uint32_t));
            append_scalar(out, kGLBMeshDiskCacheMagic);
            append_scalar(out, kGLBMeshDiskCacheVersion);
            append_scalar(out, kMeshCompilerVersion);
            append_asset_key(out, key);
            append_scalar(out, static_cast<uint8_t>(mesh.topology));
            append_scalar(out, static_cast<uint8_t>(mesh.index_format));
            append_scalar(out, static_cast<uint8_t>(mesh.has_normals));
            append_scalar(out, static_cast<uint8_t>(mesh.has_uv0));
            append_scalar(out, static_cast<uint64_t>(mesh.vertices.size()));
            append_raw_bytes(
                out,
                mesh.vertices.data(),
                mesh.vertices.size() * sizeof(MeshVertex));
            append_scalar(out, static_cast<uint64_t>(mesh.indices.size()));
            append_raw_bytes(
                out,
                mesh.indices.data(),
                mesh.indices.size() * sizeof(uint32_t));
            return out;
        }

        bool deserialize_mesh_asset(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            MeshData& mesh)
        {
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kGLBMeshDiskCacheMagic
                || version != kGLBMeshDiskCacheVersion
                || compiler_version != kMeshCompilerVersion)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            uint8_t topology = 0;
            uint8_t index_format = 0;
            uint8_t has_normals = 0;
            uint8_t has_uv0 = 0;
            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key
                || !read_scalar(bytes, offset, topology)
                || !read_scalar(bytes, offset, index_format)
                || !read_scalar(bytes, offset, has_normals)
                || !read_scalar(bytes, offset, has_uv0))
            {
                return false;
            }

            mesh.topology = static_cast<MeshPrimitiveTopology>(topology);
            mesh.index_format = static_cast<MeshIndexFormat>(index_format);
            mesh.has_normals = has_normals != 0u;
            mesh.has_uv0 = has_uv0 != 0u;

            uint64_t count = 0;
            if (!read_vector_count(bytes, offset, count, 8u * sizeof(float))) {
                return false;
            }
            mesh.vertices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    mesh.vertices.data(),
                    mesh.vertices.size() * sizeof(MeshVertex)))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(uint32_t))) {
                return false;
            }
            mesh.indices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    mesh.indices.data(),
                    mesh.indices.size() * sizeof(uint32_t)))
            {
                return false;
            }

            return offset == bytes.size();
        }

        bool load_cached_glb_mesh(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            wz::Logger& logger,
            MeshData& mesh)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }
            const wz::fs::Path path = glb_mesh_cache_path(cache, key);
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = wz::fs::read_file(path);
            if (!bytes) {
                logger.info("asset disk cache miss: glb mesh " + path);
                return false;
            }
            MeshData loaded{};
            if (!deserialize_mesh_asset(bytes.value, key, loaded)
                || !loaded.valid())
            {
                logger.warn("asset disk cache ignored invalid glb mesh: " + path);
                return false;
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            mesh = std::move(loaded);
            logger.info(
                "asset disk cache hit: glb mesh "
                + path
                + " ms="
                + std::to_string(elapsed));
            return true;
        }

        void store_cached_glb_mesh(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const MeshData& mesh,
            wz::Logger& logger)
        {
            if (!cache.enabled || cache.root.empty() || !mesh.valid()) {
                return;
            }
            const wz::fs::Path directory = glb_mesh_cache_directory(cache);
            if (wz::fs::create_directories(directory)
                != wz::fs::FileError::None)
            {
                logger.warn("asset disk cache directory unavailable: " + directory);
                return;
            }
            const wz::fs::Path path = glb_mesh_cache_path(cache, key);
            const std::vector<uint8_t> bytes = serialize_mesh_asset(key, mesh);
            const wz::fs::FileError err = wz::fs::write_file(path, bytes, true);
            if (err != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache write failed: glb mesh "
                    + path
                    + " error="
                    + std::to_string(static_cast<int>(err)));
                return;
            }
            logger.info(
                "asset disk cache stored: glb mesh "
                + path
                + " bytes="
                + std::to_string(bytes.size()));
        }

        wz::asset::AssetNode compiled_mesh_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        wz::asset::AssetNode compile_procedural_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshData(*make_mesh)())
        {
            if (!dep_nodes.empty()) {
                logger.error("procedural mesh node should not have dependencies");
                return compile_failed_node(input);
            }

            MeshData data = make_mesh();

            if (!data.valid()) {
                logger.error("procedural mesh builder produced invalid mesh data");
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(data));

            return compiled_mesh_node(input, handle);
        }

        wz::asset::AssetNode compile_glb_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::Logger& logger,
            MeshTable& mesh_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            if (dep_nodes.size() != 1) {
                logger.error("GLB mesh node should have exactly one file dependency");
                return compile_failed_node(input);
            }

            const auto* bytes =
                std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);

            if (!bytes || bytes->empty()) {
                logger.error("GLB mesh dependency did not provide file bytes");
                return compile_failed_node(input);
            }

            MeshData cached_data{};
            if (load_cached_glb_mesh(
                    cache_settings,
                    input.key,
                    logger,
                    cached_data))
            {
                const uint64_t vertex_count =
                    static_cast<uint64_t>(cached_data.vertices.size());
                const uint64_t index_count =
                    static_cast<uint64_t>(cached_data.indices.size());
                const auto store_started = std::chrono::steady_clock::now();
                wz::asset::ResourceHandle handle =
                    mesh_table.add(std::move(cached_data));
                const auto store_elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - store_started).count();
                logger.info(
                    "asset table store: cached glb mesh vertices="
                    + std::to_string(vertex_count)
                    + " indices="
                    + std::to_string(index_count)
                    + " ms="
                    + std::to_string(store_elapsed));
                if (!handle.valid()) {
                    logger.error("failed to store cached GLB mesh data");
                    return compile_failed_node(input);
                }
                return compiled_mesh_node(input, handle);
            }

            GLTFImportOptions options{};
            ImportedGLTFMeshSet imported{};

            const auto import_started = std::chrono::steady_clock::now();
            if (!import_glb_meshes(
                bytes->data(),
                bytes->size(),
                options,
                imported)) {
                logger.error("failed to import GLB mesh");
                return compile_failed_node(input);
            }
            const auto import_elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - import_started).count();
            logger.info(
                "asset compile: glb mesh import ms="
                + std::to_string(import_elapsed)
                + " bytes="
                + std::to_string(bytes->size()));

            if (imported.meshes.empty()) {
                logger.error("GLB import produced no meshes");
                return compile_failed_node(input);
            }

            uint32_t mesh_index = 0;
            if (const auto* desc = std::any_cast<GLBMeshDesc>(&input.meta))
                mesh_index = desc->mesh_index;

            if (mesh_index >= imported.meshes.size()) {
                logger.error("GLB mesh_index is out of range");
                return compile_failed_node(input);
            }

            MeshData data = std::move(imported.meshes[mesh_index].mesh);

            if (!data.valid()) {
                logger.error("GLB importer produced invalid mesh data");
                return compile_failed_node(input);
            }

            store_cached_glb_mesh(cache_settings, input.key, data, logger);

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(data));

            return compiled_mesh_node(input, handle);
        }

        wz::asset::AssetNode compile_decimated_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table)
        {
            const auto* desc =
                std::any_cast<MeshDecimationAssetDesc>(&input.meta);
            if (!desc) {
                logger.error("decimated mesh node missing compile desc");
                return compile_failed_node(input);
            }
            if (dep_handles.size() != 1u) {
                logger.error("decimated mesh node requires one mesh dependency");
                return compile_failed_node(input);
            }

            const MeshData* source = mesh_table.get(dep_handles[0]);
            if (!source || !source->valid()) {
                logger.error("decimated mesh source is invalid");
                return compile_failed_node(input);
            }

            const wz::engine::mesh_processing::MeshDecimationDesc
                processing_desc{
                    .target_vertex_count = desc->target_vertex_count,
                    .target_triangle_count = desc->target_triangle_count,
                    .target_ratio = desc->target_ratio,
                    .preserve_boundary = desc->preserve_boundary,
                    .aspect_ratio = desc->aspect_ratio,
                    .edge_length = desc->edge_length,
                    .max_valence = desc->max_valence,
                    .normal_deviation = desc->normal_deviation,
                    .hausdorff_error = desc->hausdorff_error,
                };

            wz::engine::mesh_processing::MeshProcessingResult result =
                wz::engine::mesh_processing::decimate_mesh(
                    *source,
                    processing_desc);
            if (!result.ok || !result.mesh.valid()) {
                logger.error(
                    result.error.empty()
                    ? "decimated mesh compiler produced invalid data"
                    : result.error.c_str());
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(result.mesh));
            if (!handle.valid()) {
                logger.error("failed to store decimated mesh");
                return compile_failed_node(input);
            }

            return compiled_mesh_node(input, handle);
        }

    } // anonymous namespace


    void register_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        // ── Procedural mesh compilers ─────────────────────────────────────────
        //
        // Dispatch on procedural mesh schemas.
        // These are CPU-side mesh assets: generated MeshData is stored in
        // MeshTable, and the compiled AssetNode stores the returned ResourceHandle.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralTriangleMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_triangle_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralQuadMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_quad_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kProceduralCubeMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_cube_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kPlaceholderMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_procedural_mesh_node(
                    input, dep_nodes, logger, mesh_table, &make_cube_mesh);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGLBMeshSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table, cache_settings](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_glb_mesh_node(
                    input, dep_nodes, logger, mesh_table, cache_settings);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshDecimationSchema,
            .output_type = kAssetTypeMesh,
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                return compile_decimated_mesh_node(
                    input, dep_handles, logger, mesh_table);
            }
            });
    }

} // namespace wz::engine::assets::internal
