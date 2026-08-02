// src/engine/assets/mesh/mesh_compilers.cpp

#include <engine/assets/mesh/mesh_compilers.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <engine/assets/mesh/procedural_mesh.h>
#include <engine/assets/mesh/clipmap_lattice_mesh.h>
#include <engine/assets/gltf/gltf_importer.h>
#include <engine/mesh_processing/mesh_processing.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <limits>
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

        wz::fs::Path glb_mesh_cache_directory(
            const EngineAssetCacheSettings& cache)
        {
            return disk_cache_asset_directory(
                cache,
                kGLBMeshDiskCacheKey.subdirectory);
        }

        wz::fs::Path glb_mesh_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return disk_cache_asset_path(
                cache,
                kGLBMeshDiskCacheKey.subdirectory,
                key,
                kGLBMeshDiskCacheKey.seed_lo,
                kGLBMeshDiskCacheKey.seed_hi);
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

            if (!valid_mesh_primitive_topology(topology)
                || !valid_mesh_index_format(index_format))
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

        bool load_cached_glb_mesh_impl(
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

        GLBMeshDesc glb_mesh_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            GLBMeshDesc desc{};
            desc.mesh_index =
                params.get<uint32_t>("mesh_index", desc.mesh_index);
            return desc;
        }

        MeshDecimationAssetDesc mesh_decimation_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            MeshDecimationAssetDesc desc{};
            desc.target_vertex_count =
                params.get<uint32_t>(
                    "target_vertex_count",
                    desc.target_vertex_count);
            desc.target_triangle_count =
                params.get<uint32_t>(
                    "target_triangle_count",
                    desc.target_triangle_count);
            desc.target_ratio =
                params.get<float>("target_ratio", desc.target_ratio);
            desc.preserve_boundary =
                params.get<bool>(
                    "preserve_boundary",
                    desc.preserve_boundary);
            desc.aspect_ratio =
                params.get<float>("aspect_ratio", desc.aspect_ratio);
            desc.edge_length =
                params.get<float>("edge_length", desc.edge_length);
            desc.max_valence =
                params.get<uint32_t>("max_valence", desc.max_valence);
            desc.normal_deviation =
                params.get<float>(
                    "normal_deviation",
                    desc.normal_deviation);
            desc.hausdorff_error =
                params.get<float>(
                    "hausdorff_error",
                    desc.hausdorff_error);
            return desc;
        }

        DebugTriangleStrideMeshDesc debug_triangle_stride_mesh_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            DebugTriangleStrideMeshDesc desc{};
            desc.stride = params.get<uint32_t>("stride", desc.stride);
            desc.phase = params.get<uint32_t>("phase", desc.phase);
            return desc;
        }

        // ClipmapLatticePhysicalParams + clipmap_lattice_physical_params_from_
        // params now live in engine/assets/mesh/clipmap_lattice_mesh.h, next to
        // resolve_clipmap_lattice: the ClipmapLatticeSchedule recipe reads the
        // same authored dials, and a second copy of the struct would let the two
        // recipes drift apart.

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

        wz::asset::AssetNode compile_sphere_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            wz::Logger& logger,
            MeshTable& mesh_table)
        {
            if (!dep_nodes.empty()) {
                logger.error(
                    "procedural sphere mesh node should not have dependencies");
                return compile_failed_node(input);
            }

            uint32_t subdivisions = 2u;
            float radius = 1.0f;
            if (const auto* params =
                    std::any_cast<wz::asset::ParamBlock>(&input.meta))
            {
                subdivisions =
                    params->get<uint32_t>("subdivisions", subdivisions);
                radius = params->get<float>("radius", radius);
            }

            MeshData data = make_sphere_mesh(subdivisions, radius);
            if (!data.valid()) {
                logger.error(
                    "procedural sphere mesh builder produced invalid mesh data");
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
            if (load_cached_glb_mesh_impl(
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

            GLBMeshDesc param_desc{};
            uint32_t mesh_index = 0;
            const auto* desc = std::any_cast<GLBMeshDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc = glb_mesh_desc_from_params(*params);
                    desc = &param_desc;
                }
            }
            if (desc) {
                mesh_index = desc->mesh_index;
            }

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
            MeshDecimationAssetDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshDecimationAssetDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc = mesh_decimation_desc_from_params(*params);
                    desc = &param_desc;
                }
            }
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

        MeshData make_debug_triangle_stride_mesh(
            const MeshData& source,
            uint32_t stride,
            uint32_t phase,
            wz::Logger& logger)
        {
            MeshData out{};
            out.topology = source.topology;
            out.index_format = source.index_format;
            out.has_normals = source.has_normals;
            out.has_uv0 = source.has_uv0;

            stride = stride == 0u ? 1u : stride;
            phase %= stride;

            const uint32_t triangle_count =
                static_cast<uint32_t>(source.indices.size() / 3u);
            if (triangle_count == 0u) {
                return {};
            }
            if (phase >= triangle_count) {
                phase = 0u;
            }

            std::vector<uint32_t> remap(
                source.vertices.size(),
                std::numeric_limits<uint32_t>::max());
            out.indices.reserve(
                ((triangle_count + stride - 1u) / stride) * 3u);

            const auto append_index =
                [&](uint32_t source_index) -> bool
            {
                if (source_index >= source.vertices.size()) {
                    logger.error(
                        "debug triangle stride mesh source index is out "
                        "of range");
                    return false;
                }

                uint32_t& mapped = remap[source_index];
                if (mapped == std::numeric_limits<uint32_t>::max()) {
                    if (out.vertices.size()
                        >= static_cast<size_t>(
                            std::numeric_limits<uint32_t>::max()))
                    {
                        logger.error(
                            "debug triangle stride mesh has too many "
                            "vertices");
                        return false;
                    }
                    mapped = static_cast<uint32_t>(out.vertices.size());
                    out.vertices.push_back(source.vertices[source_index]);
                }
                out.indices.push_back(mapped);
                return true;
            };

            for (uint32_t tri = phase; tri < triangle_count; tri += stride) {
                const size_t base = static_cast<size_t>(tri) * 3u;
                if (!append_index(source.indices[base])
                    || !append_index(source.indices[base + 1u])
                    || !append_index(source.indices[base + 2u]))
                {
                    return {};
                }
            }

            if (!out.valid()) {
                logger.error("debug triangle stride mesh produced no triangles");
                return {};
            }
            return out;
        }

        wz::asset::AssetNode compile_debug_triangle_stride_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table)
        {
            DebugTriangleStrideMeshDesc param_desc{};
            const auto* desc =
                std::any_cast<DebugTriangleStrideMeshDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        debug_triangle_stride_mesh_desc_from_params(*params);
                    desc = &param_desc;
                }
            }
            if (!desc) {
                logger.error("debug triangle stride mesh node missing compile desc");
                return compile_failed_node(input);
            }
            if (dep_handles.size() != 1u) {
                logger.error(
                    "debug triangle stride mesh node requires one mesh dependency");
                return compile_failed_node(input);
            }

            const MeshData* source = mesh_table.get(dep_handles[0]);
            if (!source || !source->valid()) {
                logger.error("debug triangle stride mesh source is invalid");
                return compile_failed_node(input);
            }

            MeshData data = make_debug_triangle_stride_mesh(
                *source,
                desc->stride,
                desc->phase,
                logger);
            if (!data.valid()) {
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(data));
            if (!handle.valid()) {
                logger.error("failed to store debug triangle stride mesh");
                return compile_failed_node(input);
            }

            return compiled_mesh_node(input, handle);
        }

        wz::asset::AssetNode compile_clipmap_lattice_mesh_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            ScalarFieldTable& scalar_field_table,
            ClipmapLatticeScheduleTable& clipmap_lattice_schedule_table)
        {
            // Two authoring paths produce the geometric lattice parameters:
            //
            //  (1) TYPED desc (engine-side authoring, ClipmapLatticeMeshDesc) —
            //      supplies explicit level_count / base_resolution / cell_size
            //      and is fed straight into the generator. It carries NO height
            //      field dependency: this is the explicit/bypass path used by
            //      create_clipmap_lattice_mesh and its tests.
            //
            //  (2) ParamBlock (editor / asset-graph authoring) — authors
            //      PHYSICAL parameters (world_extent, horizon, triangle_budget)
            //      and a height-field ScalarField dependency. The compiler reads
            //      the field's resolution N, computes the finest cell size
            //      s = world_extent / N, and derives the geometric lattice via
            //      resolve_clipmap_lattice(horizon, triangle_budget, s).
            ClipmapLatticeParams lattice_params{};

            if (const auto* desc =
                    std::any_cast<ClipmapLatticeMeshDesc>(&input.meta))
            {
                // Path (1): explicit geometric params, no dependency expected.
                if (!dep_handles.empty()) {
                    logger.error(
                        "clipmap lattice mesh (typed desc) should not have "
                        "dependencies");
                    return compile_failed_node(input);
                }

                lattice_params = sanitize_clipmap_lattice_params(
                    desc->level_count,
                    desc->base_resolution,
                    desc->cell_size);
            }
            else {
                // Path (2): derive from physical params + the height field's
                // resolution. Deps are resolved by TYPE, not position: the
                // height field is required, the schedule is optional, and the
                // editor is free to connect them in either order.
                const ScalarFieldData* field = nullptr;
                const ClipmapLatticeScheduleData* schedule = nullptr;
                for (size_t i = 0;
                    i < dep_nodes.size() && i < dep_handles.size();
                    ++i)
                {
                    if (dep_nodes[i].type == kAssetTypeScalarField) {
                        if (!field) {
                            field = scalar_field_table.get(dep_handles[i]);
                        }
                    }
                    else if (dep_nodes[i].type
                        == kAssetTypeClipmapLatticeSchedule)
                    {
                        if (!schedule) {
                            schedule = clipmap_lattice_schedule_table.get(
                                dep_handles[i]);
                        }
                    }
                }

                // A connected ClipmapLatticeSchedule SUPERSEDES this recipe's
                // own dials: it has already run resolve_clipmap_lattice over
                // the same authored inputs, and it is what the collision reads
                // to reconstruct the drawn surface. Deriving the lattice twice
                // is exactly the drift the schedule asset exists to remove, so
                // when one is connected we take its answer verbatim and never
                // look at world_extent / horizon / triangle_budget here.
                if (schedule && schedule->valid()) {
                    lattice_params = sanitize_clipmap_lattice_params(
                        schedule->level_count,
                        schedule->base_resolution,
                        schedule->cell_size);
                    MeshData scheduled = make_clipmap_lattice_mesh(
                        lattice_params);
                    if (!scheduled.valid()) {
                        logger.error(
                            "clipmap lattice mesh builder produced invalid "
                            "mesh data from the connected schedule");
                        return compile_failed_node(input);
                    }
                    wz::asset::ResourceHandle scheduled_handle =
                        mesh_table.add(std::move(scheduled));
                    if (!scheduled_handle.valid()) {
                        logger.error(
                            "failed to store clipmap lattice mesh");
                        return compile_failed_node(input);
                    }
                    return compiled_mesh_node(input, scheduled_handle);
                }

                if (!field || !field->valid()) {
                    logger.error(
                        "clipmap lattice mesh requires a valid height field "
                        "dependency when no schedule is connected");
                    return compile_failed_node(input);
                }

                ClipmapLatticePhysicalParams physical{};
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    physical =
                        clipmap_lattice_physical_params_from_params(*params);
                }

                // N = the field's texel count per side. The field is assumed
                // SQUARE; width is used as N (the runtime clipmap path likewise
                // keys off field->width). A non-square field would only affect
                // the derived finest cell size, never gap-free tiling.
                const uint32_t n = field->width;
                if (n == 0u) {
                    logger.error(
                        "clipmap lattice mesh height field has zero width");
                    return compile_failed_node(input);
                }

                // s = world_extent / N: the world size of one finest texel and,
                // by construction, the finest lattice cell. resolve_clipmap_
                // lattice guards a non-finite / non-positive s (falls back to
                // 1.0) and clamps the result.
                const float metres_per_texel =
                    physical.world_extent / static_cast<float>(n);

                const ResolvedClipmapLattice resolved =
                    resolve_clipmap_lattice(
                        physical.horizon,
                        static_cast<uint64_t>(physical.triangle_budget),
                        metres_per_texel);
                lattice_params = resolved.params;
            }

            MeshData data = make_clipmap_lattice_mesh(lattice_params);
            if (!data.valid()) {
                logger.error(
                    "clipmap lattice mesh builder produced invalid mesh data");
                return compile_failed_node(input);
            }

            wz::asset::ResourceHandle handle =
                mesh_table.add(std::move(data));
            if (!handle.valid()) {
                logger.error("failed to store clipmap lattice mesh");
                return compile_failed_node(input);
            }

            return compiled_mesh_node(input, handle);
        }

    } // anonymous namespace


    void register_mesh_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        ScalarFieldTable& scalar_field_table,
        ClipmapLatticeScheduleTable& clipmap_lattice_schedule_table,
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
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
            },
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
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
            },
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
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
            },
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
            .input_schema = kProceduralSphereMeshSchema,
            .output_type = kAssetTypeMesh,
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "subdivisions",
                    .type = wz::asset::ParamType::Int,
                    .label = "Subdivisions",
                    .default_num = 2,
                    .min = 0,
                    .max = 6,
                },
                {
                    .name = "radius",
                    .type = wz::asset::ParamType::Float,
                    .label = "Radius",
                    .default_num = 1.0,
                    .min = 0.0001,
                    .max = 1000000.0,
                },
            },
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                return compile_sphere_mesh_node(
                    input, dep_nodes, logger, mesh_table);
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
            .input_schema = kProceduralClipmapLatticeMeshSchema,
            .output_type = kAssetTypeMesh,
            .input_ports = {
                { "height_field", kAssetTypeScalarField },
                // Optional resolved schedule. When connected it supersedes
                // this recipe's world_extent / horizon / triangle_budget: the
                // schedule has already resolved them, and the terrain
                // collision reads the SAME schedule to reconstruct the drawn
                // surface, so the mesh and the collision cannot disagree about
                // how many rings there are or how big their cells are. When
                // absent, behaviour is unchanged (fully back-compatible).
                {
                    "schedule",
                    kAssetTypeClipmapLatticeSchedule,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "world_extent",
                    .type = wz::asset::ParamType::Float,
                    .label = "World extent",
                    .default_num = 256.0,
                    .min = 0.0001,
                    .max = 1000000.0,
                },
                {
                    .name = "horizon",
                    .type = wz::asset::ParamType::Float,
                    .label = "Horizon",
                    .default_num = 128.0,
                    .min = 0.0001,
                    .max = 1000000.0,
                },
                {
                    .name = "triangle_budget",
                    .type = wz::asset::ParamType::Int,
                    .label = "Triangle budget",
                    .default_num = 200000,
                    .min = 1,
                    .max = 100000000,
                },
            },
            .compile = [&logger, &mesh_table, &scalar_field_table,
                        &clipmap_lattice_schedule_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_clipmap_lattice_mesh_node(
                    input, dep_nodes, dep_handles, logger, mesh_table,
                    scalar_field_table, clipmap_lattice_schedule_table);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kGLBMeshSchema,
            .output_type = kAssetTypeMesh,
            .input_ports = {
                { "source_file", kAssetTypeBinaryBlob },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "mesh_index",
                    .type = wz::asset::ParamType::Int,
                    .label = "Mesh index",
                    .default_num = 0,
                    .min = 0,
                    .max = 1024,
                },
            },
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
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "target_vertex_count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Target vertices",
                    .default_num = 0,
                    .min = 0,
                    .max = 100000000,
                },
                {
                    .name = "target_triangle_count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Target triangles",
                    .default_num = 0,
                    .min = 0,
                    .max = 100000000,
                },
                {
                    .name = "target_ratio",
                    .type = wz::asset::ParamType::Float,
                    .label = "Target ratio",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "preserve_boundary",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Preserve boundary",
                    .default_num = 1.0,
                },
                {
                    .name = "aspect_ratio",
                    .type = wz::asset::ParamType::Float,
                    .label = "Aspect ratio",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "edge_length",
                    .type = wz::asset::ParamType::Float,
                    .label = "Edge length",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 100.0,
                },
                {
                    .name = "max_valence",
                    .type = wz::asset::ParamType::Int,
                    .label = "Max valence",
                    .default_num = 0,
                    .min = 0,
                    .max = 1024,
                },
                {
                    .name = "normal_deviation",
                    .type = wz::asset::ParamType::Float,
                    .label = "Normal deviation",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 180.0,
                },
                {
                    .name = "hausdorff_error",
                    .type = wz::asset::ParamType::Float,
                    .label = "Hausdorff error",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 100.0,
                },
            },
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                return compile_decimated_mesh_node(
                    input, dep_handles, logger, mesh_table);
            }
            });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kDebugTriangleStrideMeshSchema,
            .output_type = kAssetTypeMesh,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "stride",
                    .type = wz::asset::ParamType::Int,
                    .label = "Stride",
                    .default_num = 1,
                    .min = 1,
                    .max = 1000000,
                },
                {
                    .name = "phase",
                    .type = wz::asset::ParamType::Int,
                    .label = "Phase",
                    .default_num = 0,
                    .min = 0,
                    .max = 1000000,
                },
            },
            .compile = [&logger, &mesh_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode>,
                std::span<const wz::asset::ResourceHandle> dep_handles) -> wz::asset::AssetNode
            {
                return compile_debug_triangle_stride_mesh_node(
                    input, dep_handles, logger, mesh_table);
            }
            });
    }

    bool load_cached_glb_mesh(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshData& mesh)
    {
        return load_cached_glb_mesh_impl(cache, key, logger, mesh);
    }

} // namespace wz::engine::assets::internal
