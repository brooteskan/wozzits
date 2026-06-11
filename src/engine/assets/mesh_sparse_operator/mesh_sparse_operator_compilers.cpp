// src/engine/assets/mesh_sparse_operator/mesh_sparse_operator_compilers.cpp

#include <engine/assets/mesh_sparse_operator/mesh_sparse_operator_compilers.h>

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>
#include <engine/assets/mesh_sparse_operator_asset_module.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <file/filesystem.h>

#include <algorithm>
#include <any>
#include <cstring>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kMeshSparseOperatorDiskCacheMagic = 0x4f535a57u;
        constexpr uint32_t kMeshSparseOperatorDiskCacheVersion = 1u;

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

        template<typename T>
        void append_vector(
            std::vector<uint8_t>& out,
            const std::vector<T>& values)
        {
            append_scalar(out, static_cast<uint64_t>(values.size()));
            const auto* bytes =
                reinterpret_cast<const uint8_t*>(values.data());
            out.insert(
                out.end(),
                bytes,
                bytes + values.size() * sizeof(T));
        }

        template<typename T>
        bool read_vector(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            std::vector<T>& out)
        {
            uint64_t count = 0;
            if (!read_scalar(bytes, offset, count)
                || count > (bytes.size() - offset) / sizeof(T))
            {
                return false;
            }
            out.resize(static_cast<size_t>(count));
            std::memcpy(
                out.data(),
                bytes.data() + offset,
                out.size() * sizeof(T));
            offset += out.size() * sizeof(T);
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

        std::vector<uint8_t> serialize_mesh_sparse_operator(
            const wz::asset::AssetKey& key,
            const MeshSparseOperatorData& data)
        {
            std::vector<uint8_t> out;
            out.reserve(
                160u
                + data.row_offsets.size() * sizeof(uint32_t)
                + data.col_indices.size() * sizeof(uint32_t)
                + data.weights.size() * sizeof(float)
                + data.vertex_mass.size() * sizeof(float));
            append_scalar(out, kMeshSparseOperatorDiskCacheMagic);
            append_scalar(out, kMeshSparseOperatorDiskCacheVersion);
            append_scalar(out, kMeshSparseOperatorCompilerVersion);
            append_asset_key(out, key);
            append_asset_key(out, data.source_mesh_key);
            append_scalar(out, data.source_topology_hash.lo);
            append_scalar(out, data.source_topology_hash.hi);
            append_scalar(out, static_cast<uint8_t>(data.kind));
            append_scalar(out, static_cast<uint8_t>(data.domain));
            append_scalar(
                out,
                static_cast<uint8_t>(data.value_convention));
            append_scalar(out, data.row_count);
            append_scalar(out, data.nonzero_count);
            append_vector(out, data.row_offsets);
            append_vector(out, data.col_indices);
            append_vector(out, data.weights);
            append_vector(out, data.vertex_mass);
            return out;
        }

        bool deserialize_mesh_sparse_operator(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            MeshSparseOperatorData& data)
        {
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kMeshSparseOperatorDiskCacheMagic
                || version != kMeshSparseOperatorDiskCacheVersion
                || compiler_version != kMeshSparseOperatorCompilerVersion)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            uint8_t kind = 0;
            uint8_t domain = 0;
            uint8_t value_convention = 0;
            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key
                || !read_asset_key(bytes, offset, data.source_mesh_key)
                || !read_scalar(bytes, offset, data.source_topology_hash.lo)
                || !read_scalar(bytes, offset, data.source_topology_hash.hi)
                || !read_scalar(bytes, offset, kind)
                || !read_scalar(bytes, offset, domain)
                || !read_scalar(bytes, offset, value_convention)
                || !read_scalar(bytes, offset, data.row_count)
                || !read_scalar(bytes, offset, data.nonzero_count)
                || !read_vector(bytes, offset, data.row_offsets)
                || !read_vector(bytes, offset, data.col_indices)
                || !read_vector(bytes, offset, data.weights)
                || !read_vector(bytes, offset, data.vertex_mass))
            {
                return false;
            }
            data.kind = static_cast<MeshSparseOperatorKind>(kind);
            data.domain = static_cast<MeshOperatorDomain>(domain);
            data.value_convention =
                static_cast<MeshSparseOperatorValueConvention>(
                    value_convention);
            return offset == bytes.size() && data.valid();
        }

        bool store_cached_mesh_sparse_operator(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const MeshSparseOperatorData& data,
            wz::Logger& logger)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }

            const wz::fs::Path dir = disk_cache_asset_directory(
                cache,
                kMeshSparseOperatorDiskCacheKey.subdirectory);
            if (wz::fs::create_directories(dir) != wz::fs::FileError::None) {
                return false;
            }

            const wz::fs::Path path = disk_cache_asset_path(
                cache,
                kMeshSparseOperatorDiskCacheKey.subdirectory,
                key,
                kMeshSparseOperatorDiskCacheKey.seed_lo,
                kMeshSparseOperatorDiskCacheKey.seed_hi);
            const std::vector<uint8_t> bytes =
                serialize_mesh_sparse_operator(key, data);
            if (wz::fs::write_file(path, bytes) != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache store failed: mesh sparse operator "
                    + path);
                return false;
            }
            logger.info(
                "asset disk cache stored: mesh sparse operator " + path);
            return true;
        }

        // Uniform vertex Laplacian weights over the unique-edge vertex
        // adjacency graph: w_ij = 1 / degree(i), stored as NeighborWeights
        // (no diagonal), so rows sum to 1 and consumers apply
        // Lf[i] = f[i] - sum_j w_ij f[j]. Col indices come out of a sorted
        // unique edge list, so each row is strictly ascending —
        // deterministic CSR bytes by construction.
        bool build_uniform_vertex_laplacian(
            const MeshSparseOperatorDesc& desc,
            const MeshData& mesh,
            wz::Logger& logger,
            MeshSparseOperatorData& out)
        {
            if (!mesh.valid() || mesh.vertex_count() == 0u) {
                logger.error("mesh sparse operator source mesh is invalid");
                return false;
            }

            const uint32_t vertex_count = mesh.vertex_count();
            std::vector<uint64_t> directed_edges;
            directed_edges.reserve(mesh.indices.size() * 2u);
            const auto directed_edge =
                [](uint32_t from, uint32_t to) noexcept -> uint64_t
            {
                return (static_cast<uint64_t>(from) << 32u)
                    | static_cast<uint64_t>(to);
            };
            for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
                const uint32_t a = mesh.indices[i + 0u];
                const uint32_t b = mesh.indices[i + 1u];
                const uint32_t c = mesh.indices[i + 2u];
                if (a >= vertex_count
                    || b >= vertex_count
                    || c >= vertex_count)
                {
                    logger.error(
                        "mesh sparse operator index out of range");
                    return false;
                }
                // Skip degenerate triangle sides; a triangle reusing a
                // vertex contributes no self-edges.
                if (a != b) {
                    directed_edges.push_back(directed_edge(a, b));
                    directed_edges.push_back(directed_edge(b, a));
                }
                if (b != c) {
                    directed_edges.push_back(directed_edge(b, c));
                    directed_edges.push_back(directed_edge(c, b));
                }
                if (c != a) {
                    directed_edges.push_back(directed_edge(c, a));
                    directed_edges.push_back(directed_edge(a, c));
                }
            }
            std::sort(directed_edges.begin(), directed_edges.end());
            directed_edges.erase(
                std::unique(directed_edges.begin(), directed_edges.end()),
                directed_edges.end());

            out = MeshSparseOperatorData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash = compute_mesh_topology_hash(mesh),
                .kind = desc.kind,
                .domain = MeshOperatorDomain::Vertex,
                .value_convention =
                    MeshSparseOperatorValueConvention::NeighborWeights,
                .row_count = vertex_count,
                .nonzero_count =
                    static_cast<uint32_t>(directed_edges.size()),
            };

            out.row_offsets.assign(
                static_cast<size_t>(vertex_count) + 1u, 0u);
            for (const uint64_t edge : directed_edges) {
                const uint32_t from = static_cast<uint32_t>(edge >> 32u);
                ++out.row_offsets[from + 1u];
            }
            for (size_t i = 1; i < out.row_offsets.size(); ++i) {
                out.row_offsets[i] += out.row_offsets[i - 1u];
            }

            out.col_indices.reserve(directed_edges.size());
            out.weights.reserve(directed_edges.size());
            for (const uint64_t edge : directed_edges) {
                out.col_indices.push_back(
                    static_cast<uint32_t>(edge & 0xffffffffu));
            }
            for (uint32_t row = 0; row < vertex_count; ++row) {
                const uint32_t degree =
                    out.row_offsets[row + 1u] - out.row_offsets[row];
                const float weight =
                    degree > 0u ? 1.0f / static_cast<float>(degree) : 0.0f;
                for (uint32_t e = 0; e < degree; ++e) {
                    out.weights.push_back(weight);
                }
            }

            out.vertex_mass.assign(vertex_count, 1.0f);

            if (!out.valid()) {
                logger.error("mesh sparse operator output is invalid");
                return false;
            }
            return true;
        }

        bool cached_operator_matches_source(
            const MeshSparseOperatorData& data,
            const MeshSparseOperatorDesc& desc,
            const MeshData& source_mesh) noexcept
        {
            return data.valid()
                && data.source_mesh_key == desc.source_mesh.output
                && data.source_topology_hash
                    == compute_mesh_topology_hash(source_mesh)
                && data.kind == desc.kind
                && data.domain == desc.domain
                && data.row_count
                    == mesh_domain_element_count(source_mesh, desc.domain);
        }

        wz::asset::AssetNode compile_mesh_sparse_operator_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshSparseOperatorTable& operator_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            const auto* desc =
                std::any_cast<MeshSparseOperatorDesc>(&input.meta);
            if (!desc || dep_handles.size() != 1u) {
                logger.error("mesh sparse operator node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error(
                    "mesh sparse operator source mesh handle invalid");
                return compile_failed_node(input);
            }

            const auto compiled_node =
                [&input](wz::asset::ResourceHandle handle)
            {
                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            };

            MeshSparseOperatorData cached{};
            if (load_cached_mesh_sparse_operator(
                    cache_settings,
                    input.key,
                    logger,
                    cached))
            {
                if (cached_operator_matches_source(
                        cached,
                        *desc,
                        *source_mesh))
                {
                    const wz::asset::ResourceHandle handle =
                        operator_table.add(std::move(cached));
                    return handle.valid()
                        ? compiled_node(handle)
                        : compile_failed_node(input);
                }
                logger.warn(
                    "asset disk cache ignored stale mesh sparse operator");
            }

            if (desc->domain != MeshOperatorDomain::Vertex) {
                logger.error(
                    "mesh sparse operator domain not supported yet; "
                    "v0 compiles Vertex only");
                return compile_failed_node(input);
            }

            MeshSparseOperatorData data{};
            switch (desc->kind) {
            case MeshSparseOperatorKind::UniformVertexLaplacian:
                if (!build_uniform_vertex_laplacian(
                        *desc,
                        *source_mesh,
                        logger,
                        data))
                {
                    return compile_failed_node(input);
                }
                break;
            default:
                logger.error("mesh sparse operator kind not supported");
                return compile_failed_node(input);
            }

            store_cached_mesh_sparse_operator(
                cache_settings,
                input.key,
                data,
                logger);

            const wz::asset::ResourceHandle handle =
                operator_table.add(std::move(data));
            return handle.valid()
                ? compiled_node(handle)
                : compile_failed_node(input);
        }
    }

    void register_mesh_sparse_operator_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshTable& mesh_table,
        MeshSparseOperatorTable& mesh_sparse_operator_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshSparseOperatorSchema,
            .output_type = kAssetTypeMeshSparseOperator,
            .compile = [
                &logger,
                &mesh_table,
                &mesh_sparse_operator_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_sparse_operator_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_sparse_operator_table,
                    cache_settings);
            }
        });
    }

    bool load_cached_mesh_sparse_operator(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshSparseOperatorData& data)
    {
        if (!cache.enabled || cache.root.empty()) {
            return false;
        }

        const wz::fs::Path path = disk_cache_asset_path(
            cache,
            kMeshSparseOperatorDiskCacheKey.subdirectory,
            key,
            kMeshSparseOperatorDiskCacheKey.seed_lo,
            kMeshSparseOperatorDiskCacheKey.seed_hi);
        const auto bytes = wz::fs::read_file(path);
        if (!bytes) {
            logger.info(
                "asset disk cache miss: mesh sparse operator " + path);
            return false;
        }

        MeshSparseOperatorData loaded{};
        if (!deserialize_mesh_sparse_operator(bytes.value, key, loaded)) {
            logger.warn(
                "asset disk cache ignored invalid mesh sparse operator: "
                + path);
            return false;
        }

        data = std::move(loaded);
        logger.info("asset disk cache hit: mesh sparse operator " + path);
        return true;
    }
}
