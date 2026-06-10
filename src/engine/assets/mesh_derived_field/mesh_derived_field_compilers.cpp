// src/engine/assets/mesh_derived_field/mesh_derived_field_compilers.cpp

#include <engine/assets/mesh_derived_field/mesh_derived_field_compilers.h>

#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/mesh_derived_field_asset_module.h>
#include <engine/assets/mesh_derived_field/mesh_field_compute.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <file/filesystem.h>

#include <algorithm>
#include <array>
#include <any>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kMeshDerivedFieldDiskCacheMagic = 0x4d445a57u;
        constexpr uint32_t kMeshDerivedFieldDiskCacheVersion = 1u;
        constexpr uint32_t kWaveletGpuThreadGroupSize = 128u;

        struct WaveletGpuVertexSignal
        {
            float position[3] = {};
            float normal[3] = {};
        };

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

        uint32_t f32_root_constant(float value) noexcept
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
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

        void append_hash(std::vector<uint8_t>& out, const wz::asset::Hash& hash)
        {
            append_scalar(out, hash.lo);
            append_scalar(out, hash.hi);
        }

        bool read_hash(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            wz::asset::Hash& hash)
        {
            return read_scalar(bytes, offset, hash.lo)
                && read_scalar(bytes, offset, hash.hi);
        }

        struct RefVec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        RefVec3 operator+(RefVec3 a, RefVec3 b) noexcept
        {
            return RefVec3{ a.x + b.x, a.y + b.y, a.z + b.z };
        }

        RefVec3 operator-(RefVec3 a, RefVec3 b) noexcept
        {
            return RefVec3{ a.x - b.x, a.y - b.y, a.z - b.z };
        }

        RefVec3 operator*(float s, RefVec3 v) noexcept
        {
            return RefVec3{ s * v.x, s * v.y, s * v.z };
        }

        float length(RefVec3 v) noexcept
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }

        RefVec3 cross(RefVec3 a, RefVec3 b) noexcept
        {
            return RefVec3{
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x,
            };
        }

        RefVec3 normalize(RefVec3 v) noexcept
        {
            const float len = length(v);
            if (len <= 0.0f) {
                return {};
            }
            return (1.0f / len) * v;
        }

        uint64_t directed_edge_key(uint32_t from, uint32_t to) noexcept
        {
            return (static_cast<uint64_t>(from) << 32u)
                | static_cast<uint64_t>(to);
        }

        wz::fs::Path mesh_derived_field_cache_path(
            const EngineAssetCacheSettings& cache,
            const DiskCacheKeySpec& cache_key,
            const wz::asset::AssetKey& key)
        {
            return disk_cache_asset_path(
                cache,
                cache_key.subdirectory,
                key,
                cache_key.seed_lo,
                cache_key.seed_hi);
        }

        std::vector<uint8_t> serialize_mesh_derived_field_asset(
            const wz::asset::AssetKey& key,
            uint64_t compiler_version,
            const MeshDerivedFieldData& field)
        {
            std::vector<uint8_t> out;
            out.reserve(160u + field.values.size()
                + field.channels.size() * sizeof(MeshDerivedFieldChannel));
            append_scalar(out, kMeshDerivedFieldDiskCacheMagic);
            append_scalar(out, kMeshDerivedFieldDiskCacheVersion);
            append_scalar(out, compiler_version);
            append_asset_key(out, key);
            append_asset_key(out, field.source_mesh_key);
            append_hash(out, field.source_topology_hash);
            append_scalar(out, static_cast<uint8_t>(field.domain));
            append_scalar(out, field.element_count);
            append_scalar(out, static_cast<uint64_t>(field.channels.size()));
            for (const MeshDerivedFieldChannel& channel : field.channels) {
                append_scalar(out, channel.channel_id);
                append_scalar(out, static_cast<uint8_t>(channel.value_type));
                append_scalar(out, channel.byte_offset);
                append_scalar(out, channel.byte_count);
            }
            append_scalar(out, static_cast<uint64_t>(field.values.size()));
            append_raw_bytes(out, field.values.data(), field.values.size());
            return out;
        }

        bool deserialize_mesh_derived_field_asset(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            uint64_t expected_compiler_version,
            MeshDerivedFieldData& field)
        {
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kMeshDerivedFieldDiskCacheMagic
                || version != kMeshDerivedFieldDiskCacheVersion
                || compiler_version != expected_compiler_version)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            uint8_t domain = 0;
            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key
                || !read_asset_key(bytes, offset, field.source_mesh_key)
                || !read_hash(bytes, offset, field.source_topology_hash)
                || !read_scalar(bytes, offset, domain)
                || !read_scalar(bytes, offset, field.element_count))
            {
                return false;
            }
            field.domain = static_cast<MeshDerivedFieldDomain>(domain);

            uint64_t channel_count = 0;
            if (!read_scalar(bytes, offset, channel_count)) {
                return false;
            }
            const uint64_t remaining_for_channels =
                static_cast<uint64_t>(bytes.size() - offset);
            if (channel_count > remaining_for_channels / 13u) {
                return false;
            }

            field.channels.resize(static_cast<size_t>(channel_count));
            for (MeshDerivedFieldChannel& channel : field.channels) {
                uint8_t value_type = 0;
                if (!read_scalar(bytes, offset, channel.channel_id)
                    || !read_scalar(bytes, offset, value_type)
                    || !read_scalar(bytes, offset, channel.byte_offset)
                    || !read_scalar(bytes, offset, channel.byte_count))
                {
                    return false;
                }
                channel.value_type =
                    static_cast<MeshDerivedFieldValueType>(value_type);
            }

            uint64_t value_count = 0;
            if (!read_scalar(bytes, offset, value_count)
                || value_count > bytes.size() - offset)
            {
                return false;
            }
            field.values.resize(static_cast<size_t>(value_count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    field.values.data(),
                    field.values.size()))
            {
                return false;
            }
            return offset == bytes.size() && field.valid();
        }

        bool store_cached_mesh_derived_field(
            const EngineAssetCacheSettings& cache,
            const DiskCacheKeySpec& cache_key,
            const wz::asset::AssetKey& key,
            uint64_t compiler_version,
            const MeshDerivedFieldData& field,
            wz::Logger& logger)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }

            const wz::fs::Path dir = disk_cache_asset_directory(
                cache,
                cache_key.subdirectory);
            const wz::fs::FileError dir_err = wz::fs::create_directories(dir);
            if (dir_err != wz::fs::FileError::None) {
                return false;
            }

            const wz::fs::Path path =
                mesh_derived_field_cache_path(cache, cache_key, key);
            const std::vector<uint8_t> bytes =
                serialize_mesh_derived_field_asset(
                    key,
                    compiler_version,
                    field);
            const wz::fs::FileError write_err =
                wz::fs::write_file(path, bytes);
            if (write_err != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache store failed: mesh derived field "
                    + path);
                return false;
            }
            logger.info(
                "asset disk cache stored: mesh derived field " + path);
            return true;
        }

        bool load_cached_mesh_derived_field_with_key(
            const EngineAssetCacheSettings& cache,
            const DiskCacheKeySpec& cache_key,
            const wz::asset::AssetKey& key,
            uint64_t compiler_version,
            wz::Logger& logger,
            MeshDerivedFieldData& field)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }

            const wz::fs::Path path =
                mesh_derived_field_cache_path(cache, cache_key, key);
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = wz::fs::read_file(path);
            if (!bytes) {
                logger.info(
                    "asset disk cache miss: mesh derived field " + path);
                return false;
            }

            MeshDerivedFieldData loaded{};
            if (!deserialize_mesh_derived_field_asset(
                    bytes.value,
                    key,
                    compiler_version,
                    loaded))
            {
                logger.warn(
                    "asset disk cache ignored invalid mesh derived field: "
                    + path);
                return false;
            }

            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            field = std::move(loaded);
            logger.info(
                "asset disk cache hit: mesh derived field "
                + path
                + " ms="
                + std::to_string(elapsed));
            return true;
        }

        wz::asset::AssetNode compiled_field_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        bool has_duplicate_channel_ids(
            const std::vector<MeshDerivedFieldChannelDesc>& channels)
        {
            std::vector<uint32_t> ids;
            ids.reserve(channels.size());
            for (const MeshDerivedFieldChannelDesc& channel : channels) {
                ids.push_back(channel.channel_id);
            }
            std::sort(ids.begin(), ids.end());
            return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
        }

        bool pack_explicit_field(
            const wz::asset::AssetKey& input_key,
            const ExplicitMeshDerivedFieldDesc& desc,
            const MeshData& source_mesh,
            wz::Logger& logger,
            MeshDerivedFieldData& out)
        {
            if (!source_mesh.valid()) {
                logger.error("mesh derived field source mesh is invalid");
                return false;
            }
            if (desc.source_mesh.output == wz::asset::AssetKey{}
                || desc.element_count == 0u
                || desc.channels.empty()
                || has_duplicate_channel_ids(desc.channels))
            {
                logger.error("mesh derived field explicit desc is invalid");
                return false;
            }

            const uint32_t expected_count =
                mesh_domain_element_count(source_mesh, desc.domain);
            if (desc.element_count != expected_count) {
                std::ostringstream msg;
                msg
                    << "mesh derived field element_count mismatch"
                    << " expected=" << expected_count
                    << " actual=" << desc.element_count;
                logger.error(msg.str());
                return false;
            }

            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash =
                    compute_mesh_topology_hash(source_mesh),
                .domain = desc.domain,
                .element_count = desc.element_count,
            };

            uint64_t total_bytes = 0;
            for (const MeshDerivedFieldChannelDesc& channel_desc :
                desc.channels)
            {
                const uint32_t stride =
                    mesh_derived_field_value_stride(channel_desc.value_type);
                const uint64_t expected_bytes =
                    static_cast<uint64_t>(desc.element_count) * stride;
                if (channel_desc.channel_id == 0u
                    || stride == 0u
                    || channel_desc.values.size() != expected_bytes
                    || expected_bytes > UINT32_MAX
                    || total_bytes > UINT32_MAX)
                {
                    logger.error(
                        "mesh derived field channel payload is invalid");
                    return false;
                }

                out.channels.push_back(MeshDerivedFieldChannel{
                    .channel_id = channel_desc.channel_id,
                    .value_type = channel_desc.value_type,
                    .byte_offset = static_cast<uint32_t>(total_bytes),
                    .byte_count = static_cast<uint32_t>(expected_bytes),
                });
                out.values.insert(
                    out.values.end(),
                    channel_desc.values.begin(),
                    channel_desc.values.end());
                total_bytes += expected_bytes;
            }

            if (!out.valid()) {
                logger.error("mesh derived field packed data is invalid");
                return false;
            }

            (void)input_key;
            return true;
        }

        std::vector<uint32_t> build_vertex_csr(
            const MeshData& mesh,
            std::vector<uint32_t>& offsets)
        {
            std::vector<uint64_t> edges;
            edges.reserve(mesh.indices.size() * 2u);
            for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
                const uint32_t a = mesh.indices[i + 0u];
                const uint32_t b = mesh.indices[i + 1u];
                const uint32_t c = mesh.indices[i + 2u];
                edges.push_back(directed_edge_key(a, b));
                edges.push_back(directed_edge_key(b, a));
                edges.push_back(directed_edge_key(b, c));
                edges.push_back(directed_edge_key(c, b));
                edges.push_back(directed_edge_key(c, a));
                edges.push_back(directed_edge_key(a, c));
            }
            std::sort(edges.begin(), edges.end());
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

            offsets.assign(mesh.vertex_count() + 1u, 0u);
            for (const uint64_t edge : edges) {
                const uint32_t from = static_cast<uint32_t>(edge >> 32u);
                if (from + 1u < offsets.size()) {
                    ++offsets[from + 1u];
                }
            }
            for (size_t i = 1; i < offsets.size(); ++i) {
                offsets[i] += offsets[i - 1u];
            }

            std::vector<uint32_t> neighbors(edges.size(), 0u);
            std::vector<uint32_t> cursor = offsets;
            for (const uint64_t edge : edges) {
                const uint32_t from = static_cast<uint32_t>(edge >> 32u);
                const uint32_t to = static_cast<uint32_t>(edge & 0xffffffffu);
                if (from < mesh.vertex_count()) {
                    neighbors[cursor[from]++] = to;
                }
            }
            return neighbors;
        }

        std::vector<RefVec3> mesh_position_signal(const MeshData& mesh)
        {
            std::vector<RefVec3> out(mesh.vertices.size());
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                out[i] = RefVec3{
                    mesh.vertices[i].position[0],
                    mesh.vertices[i].position[1],
                    mesh.vertices[i].position[2],
                };
            }
            return out;
        }

        std::vector<RefVec3> mesh_normal_signal(const MeshData& mesh)
        {
            std::vector<RefVec3> out(mesh.vertices.size());
            if (mesh.has_normals) {
                for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                    out[i] = normalize(RefVec3{
                        mesh.vertices[i].normal[0],
                        mesh.vertices[i].normal[1],
                        mesh.vertices[i].normal[2],
                    });
                }
                return out;
            }

            for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
                const uint32_t ia = mesh.indices[i + 0u];
                const uint32_t ib = mesh.indices[i + 1u];
                const uint32_t ic = mesh.indices[i + 2u];
                const RefVec3 a = RefVec3{
                    mesh.vertices[ia].position[0],
                    mesh.vertices[ia].position[1],
                    mesh.vertices[ia].position[2],
                };
                const RefVec3 b = RefVec3{
                    mesh.vertices[ib].position[0],
                    mesh.vertices[ib].position[1],
                    mesh.vertices[ib].position[2],
                };
                const RefVec3 c = RefVec3{
                    mesh.vertices[ic].position[0],
                    mesh.vertices[ic].position[1],
                    mesh.vertices[ic].position[2],
                };
                const RefVec3 n = cross(b - a, c - a);
                out[ia] = out[ia] + n;
                out[ib] = out[ib] + n;
                out[ic] = out[ic] + n;
            }
            for (RefVec3& n : out) {
                n = normalize(n);
            }
            return out;
        }

        std::vector<RefVec3> apply_normalized_laplacian(
            const std::vector<RefVec3>& signal,
            const std::vector<uint32_t>& offsets,
            const std::vector<uint32_t>& neighbors)
        {
            std::vector<RefVec3> out(signal.size());
            for (uint32_t i = 0; i < static_cast<uint32_t>(signal.size()); ++i) {
                const uint32_t degree_i = offsets[i + 1u] - offsets[i];
                RefVec3 sum{};
                if (degree_i > 0u) {
                    for (uint32_t e = offsets[i]; e < offsets[i + 1u]; ++e) {
                        const uint32_t j = neighbors[e];
                        const uint32_t degree_j =
                            offsets[j + 1u] - offsets[j];
                        if (degree_j == 0u) {
                            continue;
                        }
                        const float weight =
                            1.0f / std::sqrt(
                                static_cast<float>(degree_i * degree_j));
                        sum = sum + weight * signal[j];
                    }
                }
                out[i] = signal[i] - sum;
            }
            return out;
        }

        std::vector<RefVec3> apply_rescaled_laplacian(
            const std::vector<RefVec3>& signal,
            const std::vector<uint32_t>& offsets,
            const std::vector<uint32_t>& neighbors,
            float lambda_max_estimate)
        {
            std::vector<RefVec3> lap =
                apply_normalized_laplacian(signal, offsets, neighbors);
            std::vector<RefVec3> out(signal.size());
            const float scale = 2.0f / lambda_max_estimate;
            for (size_t i = 0; i < signal.size(); ++i) {
                out[i] = scale * lap[i] - signal[i];
            }
            return out;
        }

        std::vector<float> normalize_energy(
            std::vector<float> values,
            float gamma)
        {
            if (values.empty()) {
                return values;
            }
            const auto [min_it, max_it] =
                std::minmax_element(values.begin(), values.end());
            const float min_value = *min_it;
            const float max_value = *max_it;
            const float range = max_value - min_value;
            if (range <= std::numeric_limits<float>::epsilon()) {
                std::fill(values.begin(), values.end(), 0.0f);
                return values;
            }
            for (float& value : values) {
                value = std::pow(
                    (std::clamp)((value - min_value) / range, 0.0f, 1.0f),
                    gamma);
            }
            return values;
        }

        std::vector<float> wavelet_energy_for_scale(
            const std::vector<RefVec3>& signal,
            const std::vector<uint32_t>& offsets,
            const std::vector<uint32_t>& neighbors,
            uint32_t scale_index,
            float lambda_max_estimate,
            float gamma)
        {
            if (signal.size() <= 3u) {
                return std::vector<float>(signal.size(), 0.0f);
            }

            std::vector<RefVec3> t_prev = signal;
            std::vector<RefVec3> t_curr = apply_rescaled_laplacian(
                signal,
                offsets,
                neighbors,
                lambda_max_estimate);
            std::vector<RefVec3> coeff(signal.size());

            const uint32_t degree = 2u + scale_index;
            for (uint32_t k = 1; k <= degree; ++k) {
                const float c =
                    1.0f / static_cast<float>((scale_index + 1u) * (k + 1u));
                for (size_t i = 0; i < coeff.size(); ++i) {
                    coeff[i] = coeff[i] + c * t_curr[i];
                }
                if (k == degree) {
                    break;
                }
                const std::vector<RefVec3> lt = apply_rescaled_laplacian(
                    t_curr,
                    offsets,
                    neighbors,
                    lambda_max_estimate);
                std::vector<RefVec3> t_next(signal.size());
                for (size_t i = 0; i < signal.size(); ++i) {
                    t_next[i] = 2.0f * lt[i] - t_prev[i];
                }
                t_prev = std::move(t_curr);
                t_curr = std::move(t_next);
            }

            std::vector<float> energy(signal.size(), 0.0f);
            for (size_t i = 0; i < coeff.size(); ++i) {
                energy[i] = length(coeff[i]);
            }
            return normalize_energy(std::move(energy), gamma);
        }

        void append_float_channel(
            MeshDerivedFieldData& out,
            uint32_t channel_id,
            const std::vector<float>& values)
        {
            const uint32_t byte_offset =
                static_cast<uint32_t>(out.values.size());
            const uint32_t byte_count =
                static_cast<uint32_t>(values.size() * sizeof(float));
            out.channels.push_back(MeshDerivedFieldChannel{
                .channel_id = channel_id,
                .value_type = MeshDerivedFieldValueType::Float1,
                .byte_offset = byte_offset,
                .byte_count = byte_count,
            });
            const auto* bytes = reinterpret_cast<const std::byte*>(
                values.data());
            out.values.insert(out.values.end(), bytes, bytes + byte_count);
        }

        bool compile_wavelet_reference(
            const MeshWaveletAnalysisDesc& desc,
            const MeshData& mesh,
            wz::Logger& logger,
            MeshDerivedFieldData& out)
        {
            if (!mesh.valid()
                || !desc.source_mesh.valid()
                || desc.scale_count == 0u
                || desc.lambda_max_estimate <= 0.0f
                || desc.gamma <= 0.0f)
            {
                logger.error("mesh wavelet analysis desc is invalid");
                return false;
            }

            std::vector<uint32_t> offsets;
            const std::vector<uint32_t> neighbors =
                build_vertex_csr(mesh, offsets);
            const std::vector<RefVec3> position = mesh_position_signal(mesh);
            const std::vector<RefVec3> normal = mesh_normal_signal(mesh);

            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash = compute_mesh_topology_hash(mesh),
                .domain = MeshDerivedFieldDomain::Vertex,
                .element_count = mesh.vertex_count(),
            };

            std::vector<std::vector<float>> position_channels;
            std::vector<std::vector<float>> normal_channels;
            position_channels.reserve(desc.scale_count);
            normal_channels.reserve(desc.scale_count);

            for (uint32_t scale = 0; scale < desc.scale_count; ++scale) {
                position_channels.push_back(wavelet_energy_for_scale(
                    position,
                    offsets,
                    neighbors,
                    scale,
                    desc.lambda_max_estimate,
                    desc.gamma));
                append_float_channel(
                    out,
                    MeshWaveletChannelID::kPositionEnergyBase + scale,
                    position_channels.back());

                normal_channels.push_back(wavelet_energy_for_scale(
                    normal,
                    offsets,
                    neighbors,
                    scale,
                    desc.lambda_max_estimate,
                    desc.gamma));
                append_float_channel(
                    out,
                    MeshWaveletChannelID::kNormalEnergyBase + scale,
                    normal_channels.back());
            }

            std::vector<float> detail(mesh.vertex_count(), 0.0f);
            for (uint32_t scale = 0; scale < desc.scale_count; ++scale) {
                for (uint32_t i = 0; i < mesh.vertex_count(); ++i) {
                    detail[i] = (std::max)(
                        detail[i],
                        0.5f * position_channels[scale][i]
                            + 0.5f * normal_channels[scale][i]);
                }
            }
            append_float_channel(
                out,
                MeshWaveletChannelID::kDetailCost,
                detail);

            if (!out.valid()) {
                logger.error("mesh wavelet analysis output is invalid");
                return false;
            }
            return true;
        }

        bool compile_wavelet_gpu_detail_heat(
            const wz::asset::AssetKey& field_key,
            const MeshWaveletAnalysisDesc& desc,
            const MeshData& mesh,
            const ComputePipelineData& pipeline_data,
            MeshFieldComputeBackend& compute,
            wz::Logger& logger,
            GpuResidentFieldTable& gpu_resident_field_table,
            MeshDerivedFieldData& out)
        {
            if (!compute.available()
                || !pipeline_data.valid()
                || !mesh.valid()
                || !desc.source_mesh.valid()
                || desc.scale_count == 0u
                || desc.lambda_max_estimate <= 0.0f
                || desc.gamma <= 0.0f)
            {
                return false;
            }

            std::vector<WaveletGpuVertexSignal> signals;
            signals.reserve(mesh.vertices.size());
            float bounds_min[3]{
                mesh.vertices[0].position[0],
                mesh.vertices[0].position[1],
                mesh.vertices[0].position[2],
            };
            float bounds_max[3]{
                mesh.vertices[0].position[0],
                mesh.vertices[0].position[1],
                mesh.vertices[0].position[2],
            };
            for (const MeshVertex& vertex : mesh.vertices) {
                WaveletGpuVertexSignal signal{};
                for (int axis = 0; axis < 3; ++axis) {
                    signal.position[axis] = vertex.position[axis];
                    signal.normal[axis] =
                        mesh.has_normals ? vertex.normal[axis]
                                         : (axis == 1 ? 1.0f : 0.0f);
                    bounds_min[axis] =
                        (std::min)(bounds_min[axis], vertex.position[axis]);
                    bounds_max[axis] =
                        (std::max)(bounds_max[axis], vertex.position[axis]);
                }
                signals.push_back(signal);
            }

            const wz::asset::ResourceHandle pipeline =
                compute.create_compute_pipeline(
                    pipeline_data,
                    pipeline_data.compute_shader);
            if (!pipeline.valid()) {
                logger.warn(
                    "mesh wavelet GPU pipeline creation failed; using reference path");
                return false;
            }

            const wz::asset::ResourceHandle input_buffer =
                compute.create_structured_buffer({
                    .element_count = mesh.vertex_count(),
                    .stride_bytes =
                        static_cast<uint32_t>(
                            sizeof(WaveletGpuVertexSignal)),
                    .initial_data = signals.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            signals.size()
                            * sizeof(WaveletGpuVertexSignal)),
                });
            const size_t output_float_count =
                static_cast<size_t>(mesh.vertex_count())
                * (static_cast<size_t>(desc.scale_count) * 2u + 1u);
            std::vector<float> zeroes(output_float_count, 0.0f);
            const wz::asset::ResourceHandle output_buffer =
                compute.create_rw_structured_buffer({
                    .element_count =
                        static_cast<uint32_t>(output_float_count),
                    .stride_bytes = sizeof(float),
                    .initial_data = zeroes.data(),
                    .initial_data_bytes =
                        static_cast<uint64_t>(
                            zeroes.size() * sizeof(float)),
                });
            if (!input_buffer.valid() || !output_buffer.valid()) {
                if (input_buffer.valid()) {
                    compute.release_buffer(input_buffer);
                }
                if (output_buffer.valid()) {
                    compute.release_buffer(output_buffer);
                }
                compute.release_pipeline(pipeline);
                logger.warn(
                    "mesh wavelet GPU buffer creation failed; using reference path");
                return false;
            }

            const float bounds_range_y =
                bounds_max[1] - bounds_min[1];
            const std::array<uint32_t, 12> root_constants{
                mesh.vertex_count(),
                desc.scale_count,
                f32_root_constant(desc.lambda_max_estimate),
                f32_root_constant(desc.gamma),
                f32_root_constant(bounds_min[0]),
                f32_root_constant(bounds_min[1]),
                f32_root_constant(bounds_min[2]),
                f32_root_constant(bounds_range_y),
                f32_root_constant(bounds_max[0]),
                f32_root_constant(bounds_max[1]),
                f32_root_constant(bounds_max[2]),
                0u,
            };
            const std::array<MeshFieldComputeBackend::DispatchBinding, 2>
                bindings{{
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = 0,
                    .register_space = 0,
                    .buffer = input_buffer,
                },
                {
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferUAV,
                    .shader_register = 0,
                    .register_space = 0,
                    .buffer = output_buffer,
                },
            }};

            const bool dispatched = compute.dispatch({
                .pipeline = pipeline,
                .bindings = bindings,
                .root_constants = root_constants,
                .group_count_x =
                    (mesh.vertex_count() + kWaveletGpuThreadGroupSize - 1u)
                    / kWaveletGpuThreadGroupSize,
                .group_count_y = 1,
                .group_count_z = 1,
            });

            std::vector<std::byte> bytes;
            if (dispatched) {
                bytes = compute.readback_buffer(output_buffer);

                if (bytes.size() == output_float_count * sizeof(float)) {
                    const uint64_t channel_byte_count =
                        static_cast<uint64_t>(mesh.vertex_count())
                        * sizeof(float);
                    for (uint32_t scale = 0; scale < desc.scale_count; ++scale) {
                        const uint64_t position_offset =
                            static_cast<uint64_t>(scale)
                            * 2u
                            * channel_byte_count;
                        const wz::asset::ResourceHandle position_resource =
                            compute.create_field_visualization_from_gpu_source(
                                output_buffer,
                                position_offset,
                                mesh.vertex_count(),
                                sizeof(float));
                        if (position_resource.valid()) {
                            const bool added = gpu_resident_field_table.add(
                                GpuResidentFieldEntry{
                                .field_key = field_key,
                                .channel_id =
                                    MeshWaveletChannelID::kPositionEnergyBase
                                    + scale,
                                .gpu_resource = position_resource,
                            });
                            if (!added) {
                                compute.release_field_visualization(
                                    position_resource);
                            }
                        }

                        const uint64_t normal_offset =
                            (static_cast<uint64_t>(scale) * 2u + 1u)
                            * channel_byte_count;
                        const wz::asset::ResourceHandle normal_resource =
                            compute.create_field_visualization_from_gpu_source(
                                output_buffer,
                                normal_offset,
                                mesh.vertex_count(),
                                sizeof(float));
                        if (normal_resource.valid()) {
                            const bool added = gpu_resident_field_table.add(
                                GpuResidentFieldEntry{
                                .field_key = field_key,
                                .channel_id =
                                    MeshWaveletChannelID::kNormalEnergyBase
                                    + scale,
                                .gpu_resource = normal_resource,
                            });
                            if (!added) {
                                compute.release_field_visualization(
                                    normal_resource);
                            }
                        }
                    }

                    const uint64_t detail_offset =
                        static_cast<uint64_t>(desc.scale_count)
                        * 2u
                        * channel_byte_count;
                    const wz::asset::ResourceHandle detail_resource =
                        compute.create_field_visualization_from_gpu_source(
                            output_buffer,
                            detail_offset,
                            mesh.vertex_count(),
                            sizeof(float));
                    if (detail_resource.valid()) {
                        const bool added = gpu_resident_field_table.add(
                            GpuResidentFieldEntry{
                            .field_key = field_key,
                            .channel_id = MeshWaveletChannelID::kDetailCost,
                            .gpu_resource = detail_resource,
                        });
                        if (!added) {
                            compute.release_field_visualization(
                                detail_resource);
                        }
                    }
                }
            }

            compute.release_buffer(input_buffer);
            compute.release_buffer(output_buffer);
            compute.release_pipeline(pipeline);

            if (!dispatched
                || bytes.size() != output_float_count * sizeof(float))
            {
                logger.warn(
                    "mesh wavelet GPU dispatch/readback failed; using reference path");
                return false;
            }

            const auto* values =
                reinterpret_cast<const float*>(bytes.data());
            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash = compute_mesh_topology_hash(mesh),
                .domain = MeshDerivedFieldDomain::Vertex,
                .element_count = mesh.vertex_count(),
            };
            for (uint32_t scale = 0; scale < desc.scale_count; ++scale) {
                const size_t position_offset =
                    static_cast<size_t>(scale) * 2u * mesh.vertex_count();
                append_float_channel(
                    out,
                    MeshWaveletChannelID::kPositionEnergyBase + scale,
                    std::vector<float>(
                        values + position_offset,
                        values + position_offset + mesh.vertex_count()));

                const size_t normal_offset =
                    (static_cast<size_t>(scale) * 2u + 1u)
                    * mesh.vertex_count();
                append_float_channel(
                    out,
                    MeshWaveletChannelID::kNormalEnergyBase + scale,
                    std::vector<float>(
                        values + normal_offset,
                        values + normal_offset + mesh.vertex_count()));
            }
            const size_t detail_offset =
                static_cast<size_t>(desc.scale_count) * 2u
                * mesh.vertex_count();
            append_float_channel(
                out,
                MeshWaveletChannelID::kDetailCost,
                std::vector<float>(
                    values + detail_offset,
                    values + detail_offset + mesh.vertex_count()));

            if (!out.valid()) {
                logger.warn(
                    "mesh wavelet GPU output invalid; using reference path");
                return false;
            }

            logger.info(
                "mesh wavelet analysis GPU path complete vertices="
                + std::to_string(mesh.vertex_count())
                + " scales=" + std::to_string(desc.scale_count));
            return true;
        }

        bool cached_field_matches_source(
            const MeshDerivedFieldData& field,
            const ExplicitMeshDerivedFieldDesc& desc,
            const MeshData& source_mesh) noexcept
        {
            return field.valid()
                && field.source_mesh_key == desc.source_mesh.output
                && field.source_topology_hash
                    == compute_mesh_topology_hash(source_mesh)
                && field.domain == desc.domain
                && field.element_count == desc.element_count;
        }

        bool cached_wavelet_field_matches_source(
            const MeshDerivedFieldData& field,
            const MeshWaveletAnalysisDesc& desc,
            const MeshData& source_mesh) noexcept
        {
            if (!field.valid()
                || field.source_mesh_key != desc.source_mesh.output
                || field.source_topology_hash
                    != compute_mesh_topology_hash(source_mesh)
                || field.domain != MeshDerivedFieldDomain::Vertex
                || field.element_count != source_mesh.vertex_count()
                || field.channels.size()
                    != static_cast<size_t>(desc.scale_count) * 2u + 1u)
            {
                return false;
            }

            const uint32_t channel_bytes =
                field.element_count * sizeof(float);
            uint32_t expected_offset = 0u;
            for (uint32_t scale = 0; scale < desc.scale_count; ++scale) {
                const MeshDerivedFieldChannel& position =
                    field.channels[scale * 2u + 0u];
                if (position.channel_id
                        != MeshWaveletChannelID::kPositionEnergyBase + scale
                    || position.value_type
                        != MeshDerivedFieldValueType::Float1
                    || position.byte_offset != expected_offset
                    || position.byte_count != channel_bytes)
                {
                    return false;
                }
                expected_offset += channel_bytes;

                const MeshDerivedFieldChannel& normal =
                    field.channels[scale * 2u + 1u];
                if (normal.channel_id
                        != MeshWaveletChannelID::kNormalEnergyBase + scale
                    || normal.value_type != MeshDerivedFieldValueType::Float1
                    || normal.byte_offset != expected_offset
                    || normal.byte_count != channel_bytes)
                {
                    return false;
                }
                expected_offset += channel_bytes;
            }

            const MeshDerivedFieldChannel& detail = field.channels.back();
            return detail.channel_id == MeshWaveletChannelID::kDetailCost
                && detail.value_type == MeshDerivedFieldValueType::Float1
                && detail.byte_offset == expected_offset
                && detail.byte_count == channel_bytes;
        }

        wz::asset::AssetNode compile_explicit_mesh_derived_field_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            const auto* desc =
                std::any_cast<ExplicitMeshDerivedFieldDesc>(&input.meta);
            if (!desc || dep_handles.size() != 1u) {
                logger.error("mesh derived field node missing explicit desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error("mesh derived field source mesh handle invalid");
                return compile_failed_node(input);
            }

            MeshDerivedFieldData cached{};
            if (load_cached_mesh_derived_field_with_key(
                    cache_settings,
                    kMeshDerivedFieldDiskCacheKey,
                    input.key,
                    kMeshDerivedFieldCompilerVersion,
                    logger,
                    cached))
            {
                if (cached_field_matches_source(cached, *desc, *source_mesh)) {
                    const wz::asset::ResourceHandle handle =
                        field_table.add(std::move(cached));
                    return handle.valid()
                        ? compiled_field_node(input, handle)
                        : compile_failed_node(input);
                }
                logger.warn(
                    "asset disk cache ignored stale mesh derived field");
            }

            MeshDerivedFieldData field{};
            if (!pack_explicit_field(
                    input.key,
                    *desc,
                    *source_mesh,
                    logger,
                    field))
            {
                return compile_failed_node(input);
            }

            store_cached_mesh_derived_field(
                cache_settings,
                kMeshDerivedFieldDiskCacheKey,
                input.key,
                kMeshDerivedFieldCompilerVersion,
                field,
                logger);

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_mesh_wavelet_analysis_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshFieldComputeBackend& mesh_field_compute,
            MeshTable& mesh_table,
            ComputePipelineTable& compute_pipeline_table,
            MeshDerivedFieldTable& field_table,
            GpuResidentFieldTable& gpu_resident_field_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            const auto* desc =
                std::any_cast<MeshWaveletAnalysisDesc>(&input.meta);
            if (!desc
                || (dep_handles.size() != 1u && dep_handles.size() != 2u))
            {
                logger.error("mesh wavelet analysis node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error("mesh wavelet analysis source mesh handle invalid");
                return compile_failed_node(input);
            }

            const ComputePipelineData* compute_pipeline =
                dep_handles.size() == 2u
                    ? compute_pipeline_table.get(dep_handles[1])
                    : nullptr;
            if (dep_handles.size() == 2u && !compute_pipeline) {
                logger.warn(
                    "mesh wavelet analysis compute pipeline dependency invalid; using reference path");
            }

            MeshDerivedFieldData cached{};
            if (load_cached_mesh_derived_field_with_key(
                    cache_settings,
                    kMeshWaveletAnalysisDiskCacheKey,
                    input.key,
                    kMeshWaveletAnalysisCompilerVersion,
                    logger,
                    cached))
            {
                if (cached_wavelet_field_matches_source(
                        cached,
                        *desc,
                        *source_mesh))
                {
                    const wz::asset::ResourceHandle handle =
                        field_table.add(std::move(cached));
                    return handle.valid()
                        ? compiled_field_node(input, handle)
                        : compile_failed_node(input);
                }
                logger.warn("asset disk cache ignored stale mesh wavelet field");
            }

            MeshDerivedFieldData field{};
            if (!(compute_pipeline
                && compile_wavelet_gpu_detail_heat(
                    input.key,
                    *desc,
                    *source_mesh,
                    *compute_pipeline,
                    mesh_field_compute,
                    logger,
                    gpu_resident_field_table,
                    field))
                && !compile_wavelet_reference(
                    *desc,
                    *source_mesh,
                    logger,
                    field))
            {
                return compile_failed_node(input);
            }

            store_cached_mesh_derived_field(
                cache_settings,
                kMeshWaveletAnalysisDiskCacheKey,
                input.key,
                kMeshWaveletAnalysisCompilerVersion,
                field,
                logger);

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_behavior_field_placeholder_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table)
        {
            const auto* desc =
                std::any_cast<BehaviorFieldPlaceholderDesc>(&input.meta);
            if (!desc || dep_handles.size() != 1u) {
                logger.error(
                    "behavior field placeholder node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh || !source_mesh->valid()) {
                logger.error(
                    "behavior field placeholder source mesh invalid");
                return compile_failed_node(input);
            }

            const uint32_t element_count =
                mesh_domain_element_count(*source_mesh, desc->domain);
            if (element_count == 0u) {
                logger.error(
                    "behavior field placeholder element_count is zero");
                return compile_failed_node(input);
            }

            const uint32_t stride = mesh_derived_field_value_stride(
                MeshDerivedFieldValueType::Float1);
            const uint64_t byte_count =
                static_cast<uint64_t>(element_count) * stride;

            MeshDerivedFieldData field{
                .source_mesh_key = desc->source_mesh.output,
                .source_topology_hash =
                    compute_mesh_topology_hash(*source_mesh),
                .domain = desc->domain,
                .element_count = element_count,
            };
            field.channels.push_back(MeshDerivedFieldChannel{
                .channel_id = desc->channel_id,
                .value_type = MeshDerivedFieldValueType::Float1,
                .byte_offset = 0,
                .byte_count = static_cast<uint32_t>(byte_count),
            });
            field.values.resize(
                static_cast<size_t>(byte_count), std::byte{0});

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }
    }

    void register_mesh_derived_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        MeshFieldComputeBackend& mesh_field_compute,
        MeshTable& mesh_table,
        ComputePipelineTable& compute_pipeline_table,
        MeshDerivedFieldTable& mesh_derived_field_table,
        GpuResidentFieldTable& gpu_resident_field_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshDerivedFieldExplicitSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_explicit_mesh_derived_field_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table,
                    cache_settings);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshWaveletAnalysisSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .compile = [
                &logger,
                &mesh_field_compute,
                &mesh_table,
                &compute_pipeline_table,
                &mesh_derived_field_table,
                &gpu_resident_field_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_wavelet_analysis_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_field_compute,
                    mesh_table,
                    compute_pipeline_table,
                    mesh_derived_field_table,
                    gpu_resident_field_table,
                    cache_settings);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kBehaviorFieldPlaceholderSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode>,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_behavior_field_placeholder_node(
                    input,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table);
            }
        });
    }

    bool load_cached_mesh_derived_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field)
    {
        return load_cached_mesh_derived_field_with_key(
            cache,
            kMeshDerivedFieldDiskCacheKey,
            key,
            kMeshDerivedFieldCompilerVersion,
            logger,
            field);
    }

    bool load_cached_mesh_wavelet_analysis_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field)
    {
        return load_cached_mesh_derived_field_with_key(
            cache,
            kMeshWaveletAnalysisDiskCacheKey,
            key,
            kMeshWaveletAnalysisCompilerVersion,
            logger,
            field);
    }
}
