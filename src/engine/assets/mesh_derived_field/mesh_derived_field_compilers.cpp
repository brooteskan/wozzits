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
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kMeshDerivedFieldDiskCacheMagic = 0x4d445a57u;
        constexpr uint32_t kMeshDerivedFieldDiskCacheVersion = 1u;
        constexpr uint32_t kWaveletGpuThreadGroupSize = 128u;

        constexpr std::array<std::string_view, 4> kDomainOptions = {
            "Vertex",
            "Edge",
            "Face",
            "Corner",
        };

        constexpr std::array<std::string_view, 5> kValueTypeOptions = {
            "Float1",
            "Float2",
            "Float3",
            "Float4",
            "UInt1",
        };

        constexpr std::array<std::string_view, 9> kBuiltinSourceOptions = {
            "Constant",
            "Position gradient",
            "Vertex index gradient",
            "Triangle corner count",
            "Vertex area",
            "Triangle area",
            "Mean edge length",
            "Inverse area density",
            "Log density",
        };

        constexpr std::array<std::string_view, 3> kBuiltinComponentOptions = {
            "X",
            "Y",
            "Z",
        };

        constexpr std::array<std::string_view, 1> kSparseApplyModeOptions = {
            "Residual",
        };

        constexpr std::array<std::string_view, 2> kSparseDiffusionModeOptions = {
            "Smooth",
            "Diffusion step",
        };

        template<class Enum, std::size_t Count>
        Enum enum_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            Enum fallback,
            const std::array<std::string_view, Count>&)
        {
            const int64_t value =
                params.get<int64_t>(name, static_cast<int64_t>(fallback));
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return static_cast<Enum>(value);
            }
            return fallback;
        }

        MeshAsset mesh_asset_from_dep(
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::size_t index)
        {
            MeshAsset asset{};
            if (dep_nodes.size() > index) {
                asset.output = dep_nodes[index].key;
            }
            return asset;
        }

        ComputePipelineAsset compute_pipeline_asset_from_dep(
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::size_t index)
        {
            ComputePipelineAsset asset{};
            if (dep_nodes.size() > index) {
                asset.key = dep_nodes[index].key;
            }
            return asset;
        }

        MeshDerivedFieldAsset mesh_field_asset_from_dep(
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::size_t index)
        {
            MeshDerivedFieldAsset asset{};
            if (dep_nodes.size() > index) {
                asset.output = dep_nodes[index].key;
            }
            return asset;
        }

        MeshSparseOperatorAsset sparse_operator_asset_from_dep(
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::size_t index)
        {
            MeshSparseOperatorAsset asset{};
            if (dep_nodes.size() > index) {
                asset.output = dep_nodes[index].key;
            }
            return asset;
        }

        ExplicitMeshDerivedFieldDesc explicit_field_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            ExplicitMeshDerivedFieldDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.domain =
                enum_param(
                    params,
                    "domain",
                    desc.domain,
                    kDomainOptions);
            desc.element_count =
                params.get<uint32_t>("element_count", desc.element_count);

            const uint32_t channel_id =
                params.get<uint32_t>("channel_id", 0x2000u);
            const MeshDerivedFieldValueType value_type =
                enum_param(
                    params,
                    "value_type",
                    MeshDerivedFieldValueType::Float1,
                    kValueTypeOptions);
            if (channel_id != 0u) {
                desc.channels.push_back(MeshDerivedFieldChannelDesc{
                    .channel_id = channel_id,
                    .value_type = value_type,
                });
            }
            return desc;
        }

        BuiltinMeshDerivedFieldDesc builtin_field_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            BuiltinMeshDerivedFieldDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.domain =
                enum_param(params, "domain", desc.domain, kDomainOptions);
            desc.channel_id =
                params.get<uint32_t>("channel_id", desc.channel_id);
            desc.value_type =
                enum_param(
                    params,
                    "value_type",
                    desc.value_type,
                    kValueTypeOptions);
            desc.source_kind =
                enum_param(
                    params,
                    "source_kind",
                    desc.source_kind,
                    kBuiltinSourceOptions);
            desc.component =
                enum_param(
                    params,
                    "component",
                    desc.component,
                    kBuiltinComponentOptions);
            desc.normalize =
                params.get<bool>("normalize", desc.normalize);
            desc.constant_value =
                params.get<float>("constant_value", desc.constant_value);
            return desc;
        }

        MeshSparseApplyFieldDesc sparse_apply_field_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshSparseApplyFieldDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.input_field = mesh_field_asset_from_dep(dep_nodes, 1u);
            desc.sparse_operator = sparse_operator_asset_from_dep(dep_nodes, 2u);
            desc.input_channel_id =
                params.get<uint32_t>(
                    "input_channel_id",
                    desc.input_channel_id);
            desc.output_channel_id =
                params.get<uint32_t>(
                    "output_channel_id",
                    desc.output_channel_id);
            desc.apply_mode =
                enum_param(
                    params,
                    "apply_mode",
                    desc.apply_mode,
                    kSparseApplyModeOptions);
            return desc;
        }

        MeshSparseDiffusionBandsDesc sparse_diffusion_bands_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshSparseDiffusionBandsDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.input_field = mesh_field_asset_from_dep(dep_nodes, 1u);
            desc.sparse_operator = sparse_operator_asset_from_dep(dep_nodes, 2u);
            desc.input_channel_id =
                params.get<uint32_t>(
                    "input_channel_id",
                    desc.input_channel_id);
            desc.output_base_channel_id =
                params.get<uint32_t>(
                    "output_base_channel_id",
                    desc.output_base_channel_id);
            desc.band_count =
                params.get<uint32_t>("band_count", desc.band_count);
            desc.iterations_per_band =
                params.get<uint32_t>(
                    "iterations_per_band",
                    desc.iterations_per_band);
            desc.mode =
                enum_param(
                    params,
                    "mode",
                    desc.mode,
                    kSparseDiffusionModeOptions);
            desc.tau = params.get<float>("tau", desc.tau);
            return desc;
        }

        MeshFieldLevelMaskDesc field_level_mask_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshFieldLevelMaskDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.input_field = mesh_field_asset_from_dep(dep_nodes, 1u);
            desc.domain =
                enum_param(params, "domain", desc.domain, kDomainOptions);
            const uint32_t input_channel_id =
                params.get<uint32_t>("input_channel_id", 0u);
            const uint32_t output_channel_id =
                params.get<uint32_t>("output_channel_id", 0u);
            if (input_channel_id != 0u && output_channel_id != 0u) {
                desc.regions.push_back(MeshFieldLevelMaskRegionDesc{
                    .input_channel_id = input_channel_id,
                    .output_channel_id = output_channel_id,
                    .min_value = params.get<float>("min_value", 0.0f),
                    .max_value = params.get<float>("max_value", 1.0f),
                });
            }
            return desc;
        }

        MeshWaveletAnalysisDesc wavelet_analysis_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshWaveletAnalysisDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.compute_pipeline = compute_pipeline_asset_from_dep(
                dep_nodes,
                1u);
            desc.scale_count =
                params.get<uint32_t>("scale_count", desc.scale_count);
            desc.lambda_max_estimate =
                params.get<float>(
                    "lambda_max_estimate",
                    desc.lambda_max_estimate);
            desc.gamma = params.get<float>("gamma", desc.gamma);
            return desc;
        }

        MeshComputeDerivedFieldDesc compute_derived_field_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            MeshComputeDerivedFieldDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.compute_pipeline = compute_pipeline_asset_from_dep(
                dep_nodes,
                1u);
            desc.domain =
                enum_param(params, "domain", desc.domain, kDomainOptions);
            const uint32_t channel_id =
                params.get<uint32_t>("channel_id", 0x2000u);
            if (channel_id != 0u) {
                desc.channels.push_back(MeshDerivedFieldChannelDesc{
                    .channel_id = channel_id,
                    .value_type =
                        enum_param(
                            params,
                            "value_type",
                            MeshDerivedFieldValueType::Float1,
                            kValueTypeOptions),
                });
            }
            desc.inputs.push_back(MeshComputeInput::Positions);
            return desc;
        }

        BehaviorFieldPlaceholderDesc behavior_placeholder_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode> dep_nodes)
        {
            BehaviorFieldPlaceholderDesc desc{};
            desc.name = params.get<std::string>("name", {});
            desc.source_mesh = mesh_asset_from_dep(dep_nodes, 0u);
            desc.domain =
                enum_param(params, "domain", desc.domain, kDomainOptions);
            desc.channel_id =
                params.get<uint32_t>("channel_id", desc.channel_id);
            return desc;
        }

        struct WaveletGpuVertexSignal
        {
            float position[3] = {};
            float normal[3] = {};
        };

        struct QuantizedPositionKey
        {
            int64_t x = 0;
            int64_t y = 0;
            int64_t z = 0;

            friend bool operator==(
                const QuantizedPositionKey& a,
                const QuantizedPositionKey& b) noexcept
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
        };

        struct QuantizedPositionKeyHash
        {
            size_t operator()(const QuantizedPositionKey& key) const noexcept
            {
                uint64_t h = 1469598103934665603ull;
                auto mix = [&h](int64_t value) {
                    uint64_t bits = 0;
                    std::memcpy(&bits, &value, sizeof(bits));
                    h ^= bits;
                    h *= 1099511628211ull;
                };
                mix(key.x);
                mix(key.y);
                mix(key.z);
                return static_cast<size_t>(h);
            }
        };

        QuantizedPositionKey quantized_position_key(
            const MeshVertex& vertex) noexcept
        {
            constexpr double kScale = 100000.0;
            return QuantizedPositionKey{
                .x = static_cast<int64_t>(
                    std::llround(vertex.position[0] * kScale)),
                .y = static_cast<int64_t>(
                    std::llround(vertex.position[1] * kScale)),
                .z = static_cast<int64_t>(
                    std::llround(vertex.position[2] * kScale)),
            };
        }

        std::string key_summary(const wz::asset::AssetKey& key)
        {
            std::ostringstream out;
            out << std::hex
                << key.content_hash.lo << ':'
                << key.content_hash.hi << ':'
                << key.schema_hash.lo << ':'
                << key.schema_hash.hi;
            return out.str();
        }

        std::string hash_summary(const wz::asset::Hash& hash)
        {
            std::ostringstream out;
            out << std::hex << hash.lo << ':' << hash.hi;
            return out.str();
        }

        const char* field_domain_name(MeshDerivedFieldDomain domain) noexcept
        {
            switch (domain) {
            case MeshDerivedFieldDomain::Vertex: return "vertex";
            case MeshDerivedFieldDomain::Face: return "face";
            case MeshDerivedFieldDomain::Edge: return "edge";
            case MeshDerivedFieldDomain::Corner: return "corner";
            }
            return "unknown";
        }

        void log_sparse_input_mesh_mismatch(
            wz::Logger& logger,
            const char* prefix,
            const MeshSparseDiffusionBandsDesc& desc,
            const MeshDerivedFieldData& input_field,
            const MeshSparseOperatorData& sparse_operator,
            const wz::asset::Hash& source_topology,
            uint32_t source_element_count)
        {
            std::ostringstream out;
            out << prefix
                << " input field does not match source mesh"
                << " expected_mesh=" << key_summary(desc.source_mesh.output)
                << " input_mesh="
                << key_summary(input_field.source_mesh_key)
                << " expected_topology=" << hash_summary(source_topology)
                << " input_topology="
                << hash_summary(input_field.source_topology_hash)
                << " expected_domain="
                << field_domain_name(sparse_operator.domain)
                << " input_domain="
                << field_domain_name(input_field.domain)
                << " expected_elements=" << source_element_count
                << " input_elements=" << input_field.element_count;
            logger.error(out.str());
        }

        void log_sparse_input_mesh_mismatch(
            wz::Logger& logger,
            const char* prefix,
            const MeshSparseApplyFieldDesc& desc,
            const MeshDerivedFieldData& input_field,
            const MeshSparseOperatorData& sparse_operator,
            const wz::asset::Hash& source_topology,
            uint32_t source_element_count)
        {
            std::ostringstream out;
            out << prefix
                << " input field does not match source mesh"
                << " expected_mesh=" << key_summary(desc.source_mesh.output)
                << " input_mesh="
                << key_summary(input_field.source_mesh_key)
                << " expected_topology=" << hash_summary(source_topology)
                << " input_topology="
                << hash_summary(input_field.source_topology_hash)
                << " expected_domain="
                << field_domain_name(sparse_operator.domain)
                << " input_domain="
                << field_domain_name(input_field.domain)
                << " expected_elements=" << source_element_count
                << " input_elements=" << input_field.element_count;
            logger.error(out.str());
        }

        void log_sparse_operator_mesh_mismatch(
            wz::Logger& logger,
            const char* prefix,
            const wz::asset::AssetKey& source_mesh_key,
            const MeshSparseOperatorData& sparse_operator,
            const wz::asset::Hash& source_topology,
            MeshDerivedFieldDomain expected_domain,
            uint32_t source_element_count)
        {
            std::ostringstream out;
            out << prefix
                << " operator does not match source mesh"
                << " expected_mesh=" << key_summary(source_mesh_key)
                << " operator_mesh="
                << key_summary(sparse_operator.source_mesh_key)
                << " expected_topology=" << hash_summary(source_topology)
                << " operator_topology="
                << hash_summary(sparse_operator.source_topology_hash)
                << " expected_domain="
                << field_domain_name(expected_domain)
                << " operator_domain="
                << field_domain_name(sparse_operator.domain)
                << " expected_rows=" << source_element_count
                << " operator_rows=" << sparse_operator.row_count;
            logger.error(out.str());
        }

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

        const char* mesh_compute_input_name(MeshComputeInput input) noexcept
        {
            switch (input) {
            case MeshComputeInput::Positions: return "positions";
            case MeshComputeInput::Normals: return "normals";
            case MeshComputeInput::UV0: return "uv0";
            case MeshComputeInput::Indices: return "indices";
            case MeshComputeInput::Vertices: return "vertices";
            }
            return "unknown";
        }

        struct MeshComputeInputBuffer
        {
            std::vector<std::byte> bytes;
            uint32_t element_count = 0;
            uint32_t stride_bytes = 0;
        };

        bool extract_mesh_compute_input(
            const MeshData& mesh,
            MeshComputeInput input,
            MeshComputeInputBuffer& out,
            std::string& diagnostic)
        {
            switch (input) {
            case MeshComputeInput::Positions: {
                out.element_count = mesh.vertex_count();
                out.stride_bytes = sizeof(float) * 3u;
                out.bytes.resize(
                    static_cast<size_t>(out.element_count)
                    * out.stride_bytes);
                for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                    std::memcpy(
                        out.bytes.data() + i * out.stride_bytes,
                        mesh.vertices[i].position,
                        out.stride_bytes);
                }
                return true;
            }
            case MeshComputeInput::Normals: {
                if (!mesh.has_normals) {
                    diagnostic = "source mesh has no normals";
                    return false;
                }
                out.element_count = mesh.vertex_count();
                out.stride_bytes = sizeof(float) * 3u;
                out.bytes.resize(
                    static_cast<size_t>(out.element_count)
                    * out.stride_bytes);
                for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                    std::memcpy(
                        out.bytes.data() + i * out.stride_bytes,
                        mesh.vertices[i].normal,
                        out.stride_bytes);
                }
                return true;
            }
            case MeshComputeInput::UV0: {
                if (!mesh.has_uv0) {
                    diagnostic = "source mesh has no uv0";
                    return false;
                }
                out.element_count = mesh.vertex_count();
                out.stride_bytes = sizeof(float) * 2u;
                out.bytes.resize(
                    static_cast<size_t>(out.element_count)
                    * out.stride_bytes);
                for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                    std::memcpy(
                        out.bytes.data() + i * out.stride_bytes,
                        mesh.vertices[i].uv,
                        out.stride_bytes);
                }
                return true;
            }
            case MeshComputeInput::Indices: {
                if (mesh.indices.empty()) {
                    diagnostic = "source mesh has no indices";
                    return false;
                }
                out.element_count = mesh.index_count();
                out.stride_bytes = sizeof(uint32_t);
                out.bytes.resize(
                    static_cast<size_t>(out.element_count)
                    * out.stride_bytes);
                std::memcpy(
                    out.bytes.data(),
                    mesh.indices.data(),
                    out.bytes.size());
                return true;
            }
            case MeshComputeInput::Vertices: {
                out.element_count = mesh.vertex_count();
                out.stride_bytes = sizeof(MeshVertex);
                out.bytes.resize(
                    static_cast<size_t>(out.element_count)
                    * out.stride_bytes);
                std::memcpy(
                    out.bytes.data(),
                    mesh.vertices.data(),
                    out.bytes.size());
                return true;
            }
            }
            diagnostic = "unknown mesh compute input";
            return false;
        }

        bool compute_derived_field_channels_valid(
            const std::vector<MeshDerivedFieldChannelDesc>& channels)
        {
            if (channels.empty()) {
                return false;
            }
            std::vector<uint32_t> ids;
            ids.reserve(channels.size());
            for (const MeshDerivedFieldChannelDesc& channel : channels) {
                if (channel.channel_id == 0u
                    || !channel.values.empty()
                    || mesh_derived_field_value_stride(channel.value_type)
                        == 0u)
                {
                    return false;
                }
                ids.push_back(channel.channel_id);
            }
            std::sort(ids.begin(), ids.end());
            return std::adjacent_find(ids.begin(), ids.end()) == ids.end();
        }

        bool compile_mesh_compute_derived_field(
            const wz::asset::AssetKey& field_key,
            const MeshComputeDerivedFieldDesc& desc,
            const MeshData& mesh,
            const ComputePipelineData& pipeline_data,
            MeshFieldComputeBackend& compute,
            wz::Logger& logger,
            GpuResidentFieldTable& gpu_resident_field_table,
            GpuResidentMeshDataTable& resident_mesh_data,
            MeshDerivedFieldData& out)
        {
            if (!mesh.valid()
                || !desc.source_mesh.valid()
                || !compute_derived_field_channels_valid(desc.channels))
            {
                logger.error("mesh compute derived field desc is invalid");
                return false;
            }
            if (!compute.available()) {
                logger.error(
                    "mesh compute derived field requires a GPU device; "
                    "no usable device is bound");
                return false;
            }
            if (!pipeline_data.valid()
                || pipeline_data.thread_group_size_x == 0u)
            {
                logger.error(
                    "mesh compute derived field compute pipeline is invalid");
                return false;
            }

            const uint32_t element_count =
                mesh_domain_element_count(mesh, desc.domain);
            if (element_count == 0u) {
                logger.error(
                    "mesh compute derived field element count is zero");
                return false;
            }

            uint64_t total_bytes = 0;
            for (const MeshDerivedFieldChannelDesc& channel : desc.channels) {
                total_bytes +=
                    static_cast<uint64_t>(element_count)
                    * mesh_derived_field_value_stride(channel.value_type);
            }
            if (total_bytes == 0u || total_bytes > UINT32_MAX) {
                logger.error(
                    "mesh compute derived field output size is invalid");
                return false;
            }

            // Positions/indices bind from the resident mesh-data table when
            // a previous compile or behavior dispatch already uploaded them;
            // anything missing is extracted here and, for those two slots,
            // registered so every later compute mesh consumer reuses the
            // upload. Other inputs stay per-compile transients.
            struct PlannedComputeInput
            {
                MeshComputeInput input = MeshComputeInput::Positions;
                MeshComputeInputBuffer data{};
                wz::asset::ResourceHandle resident{};
            };

            std::vector<PlannedComputeInput> inputs;
            inputs.reserve(desc.inputs.size());
            for (const MeshComputeInput input : desc.inputs) {
                PlannedComputeInput planned{ .input = input };
                if (input == MeshComputeInput::Positions
                    || input == MeshComputeInput::Indices)
                {
                    if (const GpuResidentMeshDataEntry* entry =
                            resident_mesh_data.find(desc.source_mesh.output))
                    {
                        planned.resident =
                            input == MeshComputeInput::Positions
                                ? entry->positions
                                : entry->indices;
                    }
                }
                if (!planned.resident.valid()) {
                    std::string diagnostic;
                    if (!extract_mesh_compute_input(
                            mesh,
                            input,
                            planned.data,
                            diagnostic))
                    {
                        logger.error(
                            std::string(
                                "mesh compute derived field input '")
                            + mesh_compute_input_name(input)
                            + "' unavailable: " + diagnostic);
                        return false;
                    }
                }
                inputs.push_back(std::move(planned));
            }

            const wz::asset::ResourceHandle pipeline =
                compute.create_compute_pipeline(
                    pipeline_data,
                    pipeline_data.compute_shader);
            if (!pipeline.valid()) {
                logger.error(
                    "mesh compute derived field pipeline creation failed");
                return false;
            }

            std::vector<wz::asset::ResourceHandle> input_buffers;
            std::vector<wz::asset::ResourceHandle> transient_buffers;
            input_buffers.reserve(inputs.size());
            bool buffers_ok = true;
            for (const PlannedComputeInput& planned : inputs) {
                if (planned.resident.valid()) {
                    input_buffers.push_back(planned.resident);
                    continue;
                }

                const wz::asset::ResourceHandle buffer =
                    compute.create_structured_buffer({
                        .element_count = planned.data.element_count,
                        .stride_bytes = planned.data.stride_bytes,
                        .initial_data = planned.data.bytes.data(),
                        .initial_data_bytes =
                            static_cast<uint64_t>(planned.data.bytes.size()),
                    });
                if (!buffer.valid()) {
                    buffers_ok = false;
                    break;
                }
                input_buffers.push_back(buffer);

                GpuResidentMeshDataEntry* entry =
                    (planned.input == MeshComputeInput::Positions
                        || planned.input == MeshComputeInput::Indices)
                        ? resident_mesh_data.find_or_add(
                            desc.source_mesh.output)
                        : nullptr;
                if (entry
                    && planned.input == MeshComputeInput::Positions
                    && !entry->positions.valid())
                {
                    entry->positions = buffer;
                    entry->vertex_count = planned.data.element_count;
                }
                else if (entry
                    && planned.input == MeshComputeInput::Indices
                    && !entry->indices.valid())
                {
                    entry->indices = buffer;
                    entry->index_count = planned.data.element_count;
                    entry->triangle_count =
                        planned.data.element_count / 3u;
                }
                else {
                    transient_buffers.push_back(buffer);
                }
            }

            const uint32_t output_dword_count =
                static_cast<uint32_t>(total_bytes / sizeof(uint32_t));
            std::vector<uint32_t> zeroes(output_dword_count, 0u);
            const wz::asset::ResourceHandle output_buffer =
                buffers_ok
                    ? compute.create_rw_structured_buffer({
                        .element_count = output_dword_count,
                        .stride_bytes = sizeof(uint32_t),
                        .initial_data = zeroes.data(),
                        .initial_data_bytes =
                            static_cast<uint64_t>(
                                zeroes.size() * sizeof(uint32_t)),
                    })
                    : wz::asset::ResourceHandle{};

            // Resident mesh buffers are owned by the table for the
            // library's lifetime; only per-compile transients are released.
            const auto release_resources = [&]()
            {
                for (const wz::asset::ResourceHandle buffer :
                    transient_buffers)
                {
                    compute.release_buffer(buffer);
                }
                if (output_buffer.valid()) {
                    compute.release_buffer(output_buffer);
                }
                compute.release_pipeline(pipeline);
            };

            if (!buffers_ok || !output_buffer.valid()) {
                release_resources();
                logger.error(
                    "mesh compute derived field buffer creation failed");
                return false;
            }

            std::vector<uint32_t> root_constants;
            root_constants.reserve(3u + desc.root_constants.size());
            root_constants.push_back(mesh.vertex_count());
            root_constants.push_back(mesh.index_count());
            root_constants.push_back(mesh.index_count() / 3u);
            root_constants.insert(
                root_constants.end(),
                desc.root_constants.begin(),
                desc.root_constants.end());

            std::vector<MeshFieldComputeBackend::DispatchBinding> bindings;
            bindings.reserve(input_buffers.size() + 1u);
            for (uint32_t i = 0;
                i < static_cast<uint32_t>(input_buffers.size());
                ++i)
            {
                bindings.push_back({
                    .kind = MeshFieldComputeBackend::BindingKind
                        ::StructuredBufferSRV,
                    .shader_register = i,
                    .register_space = 0,
                    .buffer = input_buffers[i],
                });
            }
            bindings.push_back({
                .kind = MeshFieldComputeBackend::BindingKind
                    ::StructuredBufferUAV,
                .shader_register = 0,
                .register_space = 0,
                .buffer = output_buffer,
            });

            const uint32_t group_size = pipeline_data.thread_group_size_x;
            const bool dispatched = compute.dispatch({
                .pipeline = pipeline,
                .bindings = bindings,
                .root_constants = root_constants,
                .group_count_x =
                    (element_count + group_size - 1u) / group_size,
                .group_count_y = 1,
                .group_count_z = 1,
            });

            std::vector<std::byte> bytes;
            if (dispatched) {
                bytes = compute.readback_buffer(output_buffer);

                if (bytes.size() == total_bytes) {
                    // Register channels in the GPU-resident table while the
                    // output is still device-resident so consumers skip the
                    // later re-upload. Visualization sampling is Float1-only.
                    uint64_t channel_offset = 0;
                    for (const MeshDerivedFieldChannelDesc& channel :
                        desc.channels)
                    {
                        const uint32_t stride =
                            mesh_derived_field_value_stride(
                                channel.value_type);
                        if (channel.value_type
                            == MeshDerivedFieldValueType::Float1)
                        {
                            const wz::asset::ResourceHandle resource =
                                compute
                                    .create_field_visualization_from_gpu_source(
                                        output_buffer,
                                        channel_offset,
                                        element_count,
                                        stride);
                            if (resource.valid()) {
                                const bool added =
                                    gpu_resident_field_table.add(
                                        GpuResidentFieldEntry{
                                        .field_key = field_key,
                                        .channel_id = channel.channel_id,
                                        .gpu_resource = resource,
                                    });
                                if (!added) {
                                    compute.release_field_visualization(
                                        resource);
                                }
                            }
                        }
                        channel_offset +=
                            static_cast<uint64_t>(element_count) * stride;
                    }
                }
            }

            release_resources();

            if (!dispatched || bytes.size() != total_bytes) {
                logger.error(
                    "mesh compute derived field dispatch/readback failed");
                return false;
            }

            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash = compute_mesh_topology_hash(mesh),
                .domain = desc.domain,
                .element_count = element_count,
            };
            uint32_t byte_offset = 0;
            for (const MeshDerivedFieldChannelDesc& channel : desc.channels) {
                const uint32_t byte_count =
                    element_count
                    * mesh_derived_field_value_stride(channel.value_type);
                out.channels.push_back(MeshDerivedFieldChannel{
                    .channel_id = channel.channel_id,
                    .value_type = channel.value_type,
                    .byte_offset = byte_offset,
                    .byte_count = byte_count,
                });
                byte_offset += byte_count;
            }
            out.values = std::move(bytes);

            if (!out.valid()) {
                logger.error(
                    "mesh compute derived field output is invalid");
                return false;
            }

            logger.info(
                "mesh compute derived field GPU compile complete elements="
                + std::to_string(element_count)
                + " channels=" + std::to_string(desc.channels.size()));
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

        uint32_t builtin_mesh_field_component_index(
            BuiltinMeshDerivedFieldComponent component) noexcept
        {
            switch (component) {
            case BuiltinMeshDerivedFieldComponent::X:
                return 0u;
            case BuiltinMeshDerivedFieldComponent::Y:
                return 1u;
            case BuiltinMeshDerivedFieldComponent::Z:
                return 2u;
            }
            return 1u;
        }

        float mesh_position_distance(
            const MeshVertex& a,
            const MeshVertex& b) noexcept
        {
            const float dx = a.position[0] - b.position[0];
            const float dy = a.position[1] - b.position[1];
            const float dz = a.position[2] - b.position[2];
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        float mesh_triangle_area(
            const MeshVertex& a,
            const MeshVertex& b,
            const MeshVertex& c) noexcept
        {
            const float abx = b.position[0] - a.position[0];
            const float aby = b.position[1] - a.position[1];
            const float abz = b.position[2] - a.position[2];
            const float acx = c.position[0] - a.position[0];
            const float acy = c.position[1] - a.position[1];
            const float acz = c.position[2] - a.position[2];

            const float cx = aby * acz - abz * acy;
            const float cy = abz * acx - abx * acz;
            const float cz = abx * acy - aby * acx;
            return 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
        }

        uint64_t mesh_edge_key(uint32_t a, uint32_t b) noexcept
        {
            if (a > b) {
                std::swap(a, b);
            }
            return (static_cast<uint64_t>(a) << 32u)
                | static_cast<uint64_t>(b);
        }

        void normalize_by_max_positive(std::vector<float>& values)
        {
            float max_value = 0.0f;
            for (const float value : values) {
                max_value = (std::max)(max_value, value);
            }
            if (max_value > 0.0f) {
                for (float& value : values) {
                    value /= max_value;
                }
            }
        }

        bool cached_builtin_field_matches_source(
            const MeshDerivedFieldData& field,
            const BuiltinMeshDerivedFieldDesc& desc,
            const MeshData& source_mesh) noexcept
        {
            return field.valid()
                && field.source_mesh_key == desc.source_mesh.output
                && field.source_topology_hash
                    == compute_mesh_topology_hash(source_mesh)
                && field.domain == desc.domain
                && field.element_count
                    == mesh_domain_element_count(source_mesh, desc.domain)
                && field.channels.size() == 1u
                && field.channels[0].channel_id == desc.channel_id
                && field.channels[0].value_type == desc.value_type;
        }

        std::vector<std::byte> float_channel_bytes(
            const std::vector<float>& values);

        const MeshDerivedFieldChannel* find_float_channel(
            const MeshDerivedFieldData& field,
            uint32_t channel_id) noexcept;

        bool read_float_channel_values(
            const MeshDerivedFieldData& field,
            uint32_t channel_id,
            std::vector<float>& out);

        MeshDerivedFieldData make_zero_float_field(
            wz::asset::AssetKey source_mesh_key,
            wz::asset::Hash topology,
            MeshDerivedFieldDomain domain,
            uint32_t element_count,
            std::span<const uint32_t> channel_ids);

        bool cached_field_level_mask_matches_source(
            const MeshDerivedFieldData& field,
            const MeshFieldLevelMaskDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData& input_field) noexcept
        {
            if (!field.valid()
                || field.source_mesh_key != desc.source_mesh.output
                || field.source_topology_hash
                    != compute_mesh_topology_hash(source_mesh)
                || field.domain != desc.domain
                || field.domain != input_field.domain
                || field.element_count
                    != mesh_domain_element_count(source_mesh, desc.domain)
                || field.element_count != input_field.element_count
                || field.channels.size() != desc.regions.size())
            {
                return false;
            }

            const uint32_t channel_bytes =
                field.element_count * sizeof(uint32_t);
            uint32_t expected_offset = 0u;
            for (size_t i = 0; i < desc.regions.size(); ++i) {
                const MeshDerivedFieldChannel& channel = field.channels[i];
                if (channel.channel_id
                        != desc.regions[i].output_channel_id
                    || channel.value_type
                        != MeshDerivedFieldValueType::UInt1
                    || channel.byte_offset != expected_offset
                    || channel.byte_count != channel_bytes)
                {
                    return false;
                }
                expected_offset += channel_bytes;
            }
            return true;
        }

        bool cached_sparse_apply_field_matches_source(
            const MeshDerivedFieldData& field,
            const MeshSparseApplyFieldDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData& input_field,
            const MeshSparseOperatorData& sparse_operator) noexcept
        {
            return field.valid()
                && field.source_mesh_key == desc.source_mesh.output
                && field.source_topology_hash
                    == compute_mesh_topology_hash(source_mesh)
                && field.domain == input_field.domain
                && field.domain == sparse_operator.domain
                && field.element_count
                    == mesh_domain_element_count(source_mesh, field.domain)
                && field.element_count == input_field.element_count
                && field.element_count == sparse_operator.row_count
                && field.channels.size() == 1u
                && field.channels[0].channel_id == desc.output_channel_id
                && field.channels[0].value_type
                    == MeshDerivedFieldValueType::Float1;
        }

        bool cached_sparse_diffusion_bands_matches_source(
            const MeshDerivedFieldData& field,
            const MeshSparseDiffusionBandsDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData& input_field,
            const MeshSparseOperatorData& sparse_operator) noexcept
        {
            if (!field.valid()
                || field.source_mesh_key != desc.source_mesh.output
                || field.source_topology_hash
                    != compute_mesh_topology_hash(source_mesh)
                || field.domain != input_field.domain
                || field.domain != sparse_operator.domain
                || field.element_count
                    != mesh_domain_element_count(source_mesh, field.domain)
                || field.element_count != input_field.element_count
                || field.element_count != sparse_operator.row_count
                || field.channels.size() != desc.band_count)
            {
                return false;
            }

            const uint32_t channel_bytes =
                field.element_count * sizeof(float);
            for (uint32_t band = 0; band < desc.band_count; ++band) {
                const MeshDerivedFieldChannel& channel =
                    field.channels[band];
                if (channel.channel_id
                        != desc.output_base_channel_id + band
                    || channel.value_type
                        != MeshDerivedFieldValueType::Float1
                    || channel.byte_offset != band * channel_bytes
                    || channel.byte_count != channel_bytes)
                {
                    return false;
                }
            }
            return true;
        }

        bool smooth_sparse_neighbor_weights(
            const MeshSparseOperatorData& sparse_operator,
            const std::vector<float>& input,
            std::vector<float>& output)
        {
            if (sparse_operator.row_offsets.size()
                    != static_cast<size_t>(sparse_operator.row_count) + 1u
                || input.size() != sparse_operator.row_count)
            {
                return false;
            }

            output.assign(input.size(), 0.0f);
            for (uint32_t row = 0; row < sparse_operator.row_count; ++row) {
                const uint32_t begin = sparse_operator.row_offsets[row];
                const uint32_t end = sparse_operator.row_offsets[row + 1u];
                if (begin > end
                    || end > sparse_operator.col_indices.size()
                    || end > sparse_operator.weights.size())
                {
                    return false;
                }
                if (begin == end) {
                    output[row] = input[row];
                    continue;
                }

                float value = 0.0f;
                for (uint32_t e = begin; e < end; ++e) {
                    if (sparse_operator.col_indices[e] >= input.size()) {
                        return false;
                    }
                    value +=
                        sparse_operator.weights[e]
                        * input[sparse_operator.col_indices[e]];
                }
                output[row] = value;
            }
            return true;
        }

        bool compile_mesh_sparse_diffusion_bands(
            const MeshSparseDiffusionBandsDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData& input_field,
            const MeshSparseOperatorData& sparse_operator,
            wz::Logger& logger,
            MeshDerivedFieldData& out)
        {
            if (desc.input_channel_id == 0u
                || desc.output_base_channel_id == 0u
                || desc.band_count == 0u
                || desc.iterations_per_band == 0u)
            {
                logger.error(
                    "mesh sparse diffusion bands requires nonzero channels and counts");
                return false;
            }
            if (!std::isfinite(desc.tau) || desc.tau < 0.0f) {
                logger.error("mesh sparse diffusion bands tau is invalid");
                return false;
            }
            if (!source_mesh.valid()) {
                logger.error("mesh sparse diffusion bands source mesh invalid");
                return false;
            }

            const wz::asset::Hash topology =
                compute_mesh_topology_hash(source_mesh);
            const MeshDerivedFieldDomain domain = sparse_operator.domain;
            const uint32_t element_count =
                mesh_domain_element_count(source_mesh, domain);
            if (!input_field.valid()
                || input_field.source_mesh_key != desc.source_mesh.output
                || input_field.source_topology_hash != topology
                || input_field.domain != domain
                || input_field.element_count != element_count)
            {
                log_sparse_input_mesh_mismatch(
                    logger,
                    "mesh sparse diffusion bands",
                    desc,
                    input_field,
                    sparse_operator,
                    topology,
                    element_count);
                return false;
            }
            if (!sparse_operator.valid()
                || sparse_operator.source_mesh_key != desc.source_mesh.output
                || sparse_operator.source_topology_hash != topology
                || sparse_operator.domain != domain
                || sparse_operator.row_count != element_count)
            {
                log_sparse_operator_mesh_mismatch(
                    logger,
                    "mesh sparse diffusion bands",
                    desc.source_mesh.output,
                    sparse_operator,
                    topology,
                    domain,
                    element_count);
                return false;
            }
            if (sparse_operator.value_convention
                != MeshSparseOperatorValueConvention::NeighborWeights)
            {
                logger.error(
                    "mesh sparse diffusion bands only supports NeighborWeights");
                return false;
            }

            std::vector<float> current;
            if (!read_float_channel_values(
                    input_field,
                    desc.input_channel_id,
                    current))
            {
                logger.warn(
                    "mesh sparse diffusion bands input channel missing or not Float1; emitting zero bands");
                std::vector<uint32_t> channel_ids;
                channel_ids.reserve(desc.band_count);
                for (uint32_t band = 0; band < desc.band_count; ++band) {
                    channel_ids.push_back(
                        desc.output_base_channel_id + band);
                }
                out = make_zero_float_field(
                    desc.source_mesh.output,
                    topology,
                    domain,
                    element_count,
                    channel_ids);
                return out.valid();
            }

            const uint32_t channel_bytes = element_count * sizeof(float);
            std::vector<MeshDerivedFieldChannel> channels;
            channels.reserve(desc.band_count);
            std::vector<std::byte> values;
            values.reserve(
                static_cast<size_t>(channel_bytes) * desc.band_count);

            std::vector<float> smooth;
            std::vector<float> next;
            std::vector<float> band(element_count, 0.0f);

            for (uint32_t band_index = 0;
                 band_index < desc.band_count;
                 ++band_index)
            {
                const std::vector<float> band_start = current;
                for (uint32_t iter = 0;
                     iter < desc.iterations_per_band;
                     ++iter)
                {
                    if (!smooth_sparse_neighbor_weights(
                            sparse_operator,
                            current,
                            smooth))
                    {
                        logger.warn(
                            "mesh sparse diffusion bands smoothing failed; emitting zero bands");
                        std::vector<uint32_t> channel_ids;
                        channel_ids.reserve(desc.band_count);
                        for (uint32_t band_id = 0;
                             band_id < desc.band_count;
                             ++band_id)
                        {
                            channel_ids.push_back(
                                desc.output_base_channel_id + band_id);
                        }
                        out = make_zero_float_field(
                            desc.source_mesh.output,
                            topology,
                            domain,
                            element_count,
                            channel_ids);
                        return out.valid();
                    }

                    next.resize(element_count);
                    for (uint32_t i = 0; i < element_count; ++i) {
                        switch (desc.mode) {
                        case MeshSparseDiffusionMode::Smooth:
                            next[i] = smooth[i];
                            break;
                        case MeshSparseDiffusionMode::DiffusionStep:
                            next[i] =
                                current[i]
                                + desc.tau * (smooth[i] - current[i]);
                            break;
                        }
                    }
                    current.swap(next);
                }

                for (uint32_t i = 0; i < element_count; ++i) {
                    band[i] = band_start[i] - current[i];
                }

                const uint32_t byte_offset =
                    static_cast<uint32_t>(values.size());
                std::vector<std::byte> band_bytes =
                    float_channel_bytes(band);
                values.insert(
                    values.end(),
                    band_bytes.begin(),
                    band_bytes.end());
                channels.push_back(MeshDerivedFieldChannel{
                    .channel_id =
                        desc.output_base_channel_id + band_index,
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .byte_offset = byte_offset,
                    .byte_count = channel_bytes,
                });
            }

            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash = topology,
                .domain = domain,
                .element_count = element_count,
                .channels = std::move(channels),
                .values = std::move(values),
            };
            if (!out.valid()) {
                logger.error(
                    "mesh sparse diffusion bands output field is invalid");
                return false;
            }
            return true;
        }

        bool compile_mesh_sparse_apply_field(
            const MeshSparseApplyFieldDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData& input_field,
            const MeshSparseOperatorData& sparse_operator,
            wz::Logger& logger,
            MeshDerivedFieldData& out)
        {
            if (desc.apply_mode != MeshSparseApplyMode::Residual) {
                logger.error("mesh sparse apply only supports Residual mode");
                return false;
            }
            if (desc.input_channel_id == 0u
                || desc.output_channel_id == 0u)
            {
                logger.error(
                    "mesh sparse apply requires nonzero channel ids");
                return false;
            }
            if (!source_mesh.valid()) {
                logger.error("mesh sparse apply source mesh invalid");
                return false;
            }

            const wz::asset::Hash topology =
                compute_mesh_topology_hash(source_mesh);
            const MeshDerivedFieldDomain domain = sparse_operator.domain;
            const uint32_t element_count =
                mesh_domain_element_count(source_mesh, domain);
            if (!input_field.valid()
                || input_field.source_mesh_key != desc.source_mesh.output
                || input_field.source_topology_hash != topology
                || input_field.domain != domain
                || input_field.element_count != element_count)
            {
                log_sparse_input_mesh_mismatch(
                    logger,
                    "mesh sparse apply",
                    desc,
                    input_field,
                    sparse_operator,
                    topology,
                    element_count);
                return false;
            }
            if (!sparse_operator.valid()
                || sparse_operator.source_mesh_key != desc.source_mesh.output
                || sparse_operator.source_topology_hash != topology
                || sparse_operator.domain != domain
                || sparse_operator.row_count != element_count)
            {
                log_sparse_operator_mesh_mismatch(
                    logger,
                    "mesh sparse apply",
                    desc.source_mesh.output,
                    sparse_operator,
                    topology,
                    domain,
                    element_count);
                return false;
            }
            if (sparse_operator.value_convention
                != MeshSparseOperatorValueConvention::NeighborWeights)
            {
                logger.error(
                    "mesh sparse apply only supports NeighborWeights");
                return false;
            }

            std::vector<float> input_values;
            if (!read_float_channel_values(
                    input_field,
                    desc.input_channel_id,
                    input_values))
            {
                logger.warn(
                    "mesh sparse apply input channel missing or not Float1; emitting zero field");
                const uint32_t channel_id = desc.output_channel_id;
                out = make_zero_float_field(
                    desc.source_mesh.output,
                    topology,
                    domain,
                    element_count,
                    std::span<const uint32_t>(&channel_id, 1u));
                return out.valid();
            }

            std::vector<float> output_values(element_count, 0.0f);
            if (sparse_operator.row_offsets.size()
                != static_cast<size_t>(element_count) + 1u)
            {
                logger.warn(
                    "mesh sparse apply operator row offsets invalid; emitting zero field");
                const uint32_t channel_id = desc.output_channel_id;
                out = make_zero_float_field(
                    desc.source_mesh.output,
                    topology,
                    domain,
                    element_count,
                    std::span<const uint32_t>(&channel_id, 1u));
                return out.valid();
            }
            for (uint32_t row = 0; row < element_count; ++row) {
                const uint32_t begin = sparse_operator.row_offsets[row];
                const uint32_t end = sparse_operator.row_offsets[row + 1u];
                if (begin > end
                    || end > sparse_operator.col_indices.size()
                    || end > sparse_operator.weights.size())
                {
                    logger.warn(
                        "mesh sparse apply operator row range invalid; emitting zero field");
                    const uint32_t channel_id = desc.output_channel_id;
                    out = make_zero_float_field(
                        desc.source_mesh.output,
                        topology,
                        domain,
                        element_count,
                        std::span<const uint32_t>(&channel_id, 1u));
                    return out.valid();
                }
                if (begin == end) {
                    output_values[row] = 0.0f;
                    continue;
                }

                float neighbor_average = 0.0f;
                for (uint32_t e = begin; e < end; ++e) {
                    if (sparse_operator.col_indices[e]
                        >= input_values.size())
                    {
                        logger.warn(
                            "mesh sparse apply operator column invalid; emitting zero field");
                        const uint32_t channel_id = desc.output_channel_id;
                        out = make_zero_float_field(
                            desc.source_mesh.output,
                            topology,
                            domain,
                            element_count,
                            std::span<const uint32_t>(&channel_id, 1u));
                        return out.valid();
                    }
                    neighbor_average +=
                        sparse_operator.weights[e]
                        * input_values[sparse_operator.col_indices[e]];
                }
                output_values[row] = input_values[row] - neighbor_average;
            }

            std::vector<std::byte> output_bytes =
                float_channel_bytes(output_values);
            const uint32_t byte_count =
                static_cast<uint32_t>(output_bytes.size());
            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash = topology,
                .domain = domain,
                .element_count = element_count,
                .channels = {
                    MeshDerivedFieldChannel{
                        .channel_id = desc.output_channel_id,
                        .value_type = MeshDerivedFieldValueType::Float1,
                        .byte_offset = 0u,
                        .byte_count = byte_count,
                    },
                },
                .values = std::move(output_bytes),
            };
            if (!out.valid()) {
                logger.error("mesh sparse apply output field is invalid");
                return false;
            }
            return true;
        }

        std::vector<std::byte> float_channel_bytes(
            const std::vector<float>& values)
        {
            std::vector<std::byte> bytes(
                values.size() * sizeof(float),
                std::byte{0});
            if (!values.empty()) {
                std::memcpy(bytes.data(), values.data(), bytes.size());
            }
            return bytes;
        }

        const MeshDerivedFieldChannel* find_float_channel(
            const MeshDerivedFieldData& field,
            uint32_t channel_id) noexcept
        {
            for (const MeshDerivedFieldChannel& channel : field.channels) {
                if (channel.channel_id == channel_id
                    && channel.value_type
                        == MeshDerivedFieldValueType::Float1)
                {
                    return &channel;
                }
            }
            return nullptr;
        }

        bool read_float_channel_values(
            const MeshDerivedFieldData& field,
            uint32_t channel_id,
            std::vector<float>& out)
        {
            const MeshDerivedFieldChannel* channel =
                find_float_channel(field, channel_id);
            if (!channel) {
                return false;
            }
            const size_t byte_offset = channel->byte_offset;
            const size_t byte_count = channel->byte_count;
            if (byte_offset > field.values.size()
                || byte_count > field.values.size() - byte_offset
                || byte_count
                    != static_cast<size_t>(field.element_count)
                        * sizeof(float))
            {
                return false;
            }
            out.resize(field.element_count);
            std::memcpy(
                out.data(),
                field.values.data() + byte_offset,
                byte_count);
            return true;
        }

        MeshDerivedFieldData make_zero_float_field(
            wz::asset::AssetKey source_mesh_key,
            wz::asset::Hash topology,
            MeshDerivedFieldDomain domain,
            uint32_t element_count,
            std::span<const uint32_t> channel_ids)
        {
            const uint32_t channel_bytes = element_count * sizeof(float);
            std::vector<MeshDerivedFieldChannel> channels;
            channels.reserve(channel_ids.size());
            std::vector<std::byte> values(
                static_cast<size_t>(channel_bytes) * channel_ids.size(),
                std::byte{0});

            for (size_t i = 0; i < channel_ids.size(); ++i) {
                channels.push_back(MeshDerivedFieldChannel{
                    .channel_id = channel_ids[i],
                    .value_type = MeshDerivedFieldValueType::Float1,
                    .byte_offset =
                        static_cast<uint32_t>(i * channel_bytes),
                    .byte_count = channel_bytes,
                });
            }

            return MeshDerivedFieldData{
                .source_mesh_key = source_mesh_key,
                .source_topology_hash = topology,
                .domain = domain,
                .element_count = element_count,
                .channels = std::move(channels),
                .values = std::move(values),
            };
        }

        std::vector<std::byte> uint_channel_bytes(
            const std::vector<uint32_t>& values)
        {
            std::vector<std::byte> bytes(
                values.size() * sizeof(uint32_t),
                std::byte{0});
            if (!values.empty()) {
                std::memcpy(bytes.data(), values.data(), bytes.size());
            }
            return bytes;
        }

        bool compile_mesh_field_level_mask(
            const MeshFieldLevelMaskDesc& desc,
            const MeshData& source_mesh,
            const MeshDerivedFieldData& input_field,
            wz::Logger& logger,
            MeshDerivedFieldData& out)
        {
            if (desc.domain != MeshDerivedFieldDomain::Vertex
                && desc.domain != MeshDerivedFieldDomain::Face)
            {
                logger.error(
                    "mesh field level mask only supports vertex or face domain");
                return false;
            }
            if (desc.regions.empty()) {
                logger.error("mesh field level mask requires regions");
                return false;
            }
            if (!source_mesh.valid()) {
                logger.error("mesh field level mask source mesh invalid");
                return false;
            }

            const wz::asset::Hash topology =
                compute_mesh_topology_hash(source_mesh);
            const uint32_t element_count =
                mesh_domain_element_count(source_mesh, desc.domain);
            if (!input_field.valid()
                || input_field.source_mesh_key != desc.source_mesh.output
                || input_field.source_topology_hash != topology
                || input_field.domain != desc.domain
                || input_field.element_count != element_count)
            {
                logger.error(
                    "mesh field level mask input field does not match source mesh");
                return false;
            }

            const uint32_t channel_bytes =
                element_count * sizeof(uint32_t);
            std::vector<MeshDerivedFieldChannel> channels;
            channels.reserve(desc.regions.size());
            std::vector<std::byte> values;
            values.reserve(
                static_cast<size_t>(channel_bytes) * desc.regions.size());

            for (const MeshFieldLevelMaskRegionDesc& region :
                 desc.regions)
            {
                if (region.input_channel_id == 0u
                    || region.output_channel_id == 0u
                    || !std::isfinite(region.min_value)
                    || !std::isfinite(region.max_value))
                {
                    logger.error(
                        "mesh field level mask region has invalid parameters");
                    return false;
                }

                float min_value = region.min_value;
                float max_value = region.max_value;
                if (min_value > max_value) {
                    std::swap(min_value, max_value);
                }

                std::vector<uint32_t> mask_values(element_count, 0u);
                std::vector<float> input_values;
                if (!read_float_channel_values(
                        input_field,
                        region.input_channel_id,
                        input_values))
                {
                    logger.warn(
                        "mesh field level mask input channel missing or not Float1; emitting zero mask channel");
                } else {
                    for (uint32_t i = 0; i < element_count; ++i) {
                        const float value = input_values[i];
                        mask_values[i] =
                            std::isfinite(value)
                                && value >= min_value
                                && value <= max_value
                            ? 1u
                            : 0u;
                    }
                }

                const uint32_t byte_offset =
                    static_cast<uint32_t>(values.size());
                std::vector<std::byte> region_bytes =
                    uint_channel_bytes(mask_values);
                values.insert(
                    values.end(),
                    region_bytes.begin(),
                    region_bytes.end());
                channels.push_back(MeshDerivedFieldChannel{
                    .channel_id = region.output_channel_id,
                    .value_type = MeshDerivedFieldValueType::UInt1,
                    .byte_offset = byte_offset,
                    .byte_count = channel_bytes,
                });
            }

            out = MeshDerivedFieldData{
                .source_mesh_key = desc.source_mesh.output,
                .source_topology_hash = topology,
                .domain = desc.domain,
                .element_count = element_count,
                .channels = std::move(channels),
                .values = std::move(values),
            };
            if (!out.valid()) {
                logger.error("mesh field level mask output is invalid");
                return false;
            }
            return true;
        }

        bool build_builtin_mesh_derived_field_values(
            const BuiltinMeshDerivedFieldDesc& desc,
            const MeshData& mesh,
            std::vector<float>& values,
            wz::Logger& logger)
        {
            if (desc.domain != MeshDerivedFieldDomain::Vertex
                && desc.domain != MeshDerivedFieldDomain::Face)
            {
                logger.error(
                    "builtin mesh derived field only supports vertex or face domain");
                return false;
            }
            if (desc.value_type != MeshDerivedFieldValueType::Float1) {
                logger.error(
                    "builtin mesh derived field only supports Float1");
                return false;
            }
            if (desc.channel_id == 0u) {
                logger.error(
                    "builtin mesh derived field channel_id is invalid");
                return false;
            }

            const uint32_t vertex_count = mesh.vertex_count();
            const uint32_t element_count =
                mesh_domain_element_count(mesh, desc.domain);
            values.assign(element_count, 0.0f);

            switch (desc.source_kind) {
            case BuiltinMeshDerivedFieldSourceKind::Constant:
                std::fill(
                    values.begin(),
                    values.end(),
                    desc.constant_value);
                return true;

            case BuiltinMeshDerivedFieldSourceKind::PositionGradient: {
                if (desc.domain != MeshDerivedFieldDomain::Vertex) {
                    logger.error(
                        "position_gradient requires vertex domain");
                    return false;
                }
                const uint32_t component =
                    builtin_mesh_field_component_index(desc.component);
                float min_value = 0.0f;
                float max_value = 0.0f;
                if (!mesh.vertices.empty()) {
                    min_value = mesh.vertices.front().position[component];
                    max_value = min_value;
                }
                for (const MeshVertex& vertex : mesh.vertices) {
                    const float value = vertex.position[component];
                    min_value = (std::min)(min_value, value);
                    max_value = (std::max)(max_value, value);
                }
                const float range = max_value - min_value;
                for (uint32_t i = 0; i < vertex_count; ++i) {
                    const float value = mesh.vertices[i].position[component];
                    values[i] =
                        desc.normalize && range > 0.0f
                            ? (value - min_value) / range
                            : value;
                }
                return true;
            }

            case BuiltinMeshDerivedFieldSourceKind::VertexIndexGradient:
                if (desc.domain != MeshDerivedFieldDomain::Vertex) {
                    logger.error(
                        "vertex_index_gradient requires vertex domain");
                    return false;
                }
                for (uint32_t i = 0; i < vertex_count; ++i) {
                    values[i] =
                        desc.normalize
                            ? (vertex_count > 1u
                                ? static_cast<float>(i)
                                    / static_cast<float>(vertex_count - 1u)
                                : 0.0f)
                            : static_cast<float>(i);
                }
                return true;

            case BuiltinMeshDerivedFieldSourceKind::TriangleCornerCount: {
                if (desc.domain != MeshDerivedFieldDomain::Vertex) {
                    logger.error(
                        "triangle_corner_count requires vertex domain");
                    return false;
                }
                std::unordered_map<
                    QuantizedPositionKey,
                    float,
                    QuantizedPositionKeyHash>
                    corner_counts_by_position;
                corner_counts_by_position.reserve(vertex_count);

                for (const uint32_t index : mesh.indices) {
                    if (index < vertex_count) {
                        corner_counts_by_position[
                            quantized_position_key(mesh.vertices[index])]
                            += 1.0f;
                    }
                }
                for (uint32_t i = 0; i < vertex_count; ++i) {
                    const auto found = corner_counts_by_position.find(
                        quantized_position_key(mesh.vertices[i]));
                    if (found != corner_counts_by_position.end()) {
                        values[i] = found->second;
                    }
                }
                if (desc.normalize) {
                    normalize_by_max_positive(values);
                }
                return true;
            }

            case BuiltinMeshDerivedFieldSourceKind::VertexArea: {
                if (desc.domain != MeshDerivedFieldDomain::Vertex) {
                    logger.error("vertex_area requires vertex domain");
                    return false;
                }
                for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
                    const uint32_t ia = mesh.indices[i + 0u];
                    const uint32_t ib = mesh.indices[i + 1u];
                    const uint32_t ic = mesh.indices[i + 2u];
                    if (ia >= vertex_count
                        || ib >= vertex_count
                        || ic >= vertex_count)
                    {
                        continue;
                    }
                    const float area =
                        mesh_triangle_area(
                            mesh.vertices[ia],
                            mesh.vertices[ib],
                            mesh.vertices[ic])
                        / 3.0f;
                    values[ia] += area;
                    values[ib] += area;
                    values[ic] += area;
                }
                if (desc.normalize) {
                    normalize_by_max_positive(values);
                }
                return true;
            }

            case BuiltinMeshDerivedFieldSourceKind::TriangleArea: {
                if (desc.domain != MeshDerivedFieldDomain::Face) {
                    logger.error("triangle_area requires face domain");
                    return false;
                }
                for (uint32_t face = 0; face < element_count; ++face) {
                    const size_t i = static_cast<size_t>(face) * 3u;
                    const uint32_t ia = mesh.indices[i + 0u];
                    const uint32_t ib = mesh.indices[i + 1u];
                    const uint32_t ic = mesh.indices[i + 2u];
                    if (ia >= vertex_count
                        || ib >= vertex_count
                        || ic >= vertex_count)
                    {
                        continue;
                    }
                    values[face] =
                        mesh_triangle_area(
                            mesh.vertices[ia],
                            mesh.vertices[ib],
                            mesh.vertices[ic]);
                }
                if (desc.normalize) {
                    normalize_by_max_positive(values);
                }
                return true;
            }

            case BuiltinMeshDerivedFieldSourceKind::MeanEdgeLength: {
                if (desc.domain == MeshDerivedFieldDomain::Face) {
                    for (uint32_t face = 0; face < element_count; ++face) {
                        const size_t i = static_cast<size_t>(face) * 3u;
                        const uint32_t ia = mesh.indices[i + 0u];
                        const uint32_t ib = mesh.indices[i + 1u];
                        const uint32_t ic = mesh.indices[i + 2u];
                        if (ia >= vertex_count
                            || ib >= vertex_count
                            || ic >= vertex_count)
                        {
                            continue;
                        }
                        const float ab = mesh_position_distance(
                            mesh.vertices[ia],
                            mesh.vertices[ib]);
                        const float bc = mesh_position_distance(
                            mesh.vertices[ib],
                            mesh.vertices[ic]);
                        const float ca = mesh_position_distance(
                            mesh.vertices[ic],
                            mesh.vertices[ia]);
                        values[face] = (ab + bc + ca) / 3.0f;
                    }
                    if (desc.normalize) {
                        normalize_by_max_positive(values);
                    }
                    return true;
                }
                if (desc.domain != MeshDerivedFieldDomain::Vertex) {
                    logger.error(
                        "mean_edge_length requires vertex or face domain");
                    return false;
                }
                std::vector<float> length_sums(vertex_count, 0.0f);
                std::vector<uint32_t> length_counts(vertex_count, 0u);
                std::unordered_set<uint64_t> seen_edges;
                seen_edges.reserve(mesh.indices.size());

                auto add_edge = [&](uint32_t a, uint32_t b)
                {
                    if (a >= vertex_count || b >= vertex_count || a == b) {
                        return;
                    }
                    const uint64_t key = mesh_edge_key(a, b);
                    if (!seen_edges.insert(key).second) {
                        return;
                    }

                    const float length =
                        mesh_position_distance(mesh.vertices[a], mesh.vertices[b]);
                    length_sums[a] += length;
                    length_sums[b] += length;
                    ++length_counts[a];
                    ++length_counts[b];
                };

                for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
                    const uint32_t ia = mesh.indices[i + 0u];
                    const uint32_t ib = mesh.indices[i + 1u];
                    const uint32_t ic = mesh.indices[i + 2u];
                    add_edge(ia, ib);
                    add_edge(ib, ic);
                    add_edge(ic, ia);
                }

                for (uint32_t i = 0; i < vertex_count; ++i) {
                    values[i] =
                        length_counts[i] > 0u
                            ? length_sums[i]
                                / static_cast<float>(length_counts[i])
                            : 0.0f;
                }
                if (desc.normalize) {
                    normalize_by_max_positive(values);
                }
                return true;
            }

            case BuiltinMeshDerivedFieldSourceKind::InverseAreaDensity:
            case BuiltinMeshDerivedFieldSourceKind::LogDensity: {
                constexpr float kAreaEpsilon = 1.0e-20f;
                if (desc.domain == MeshDerivedFieldDomain::Face) {
                    for (uint32_t face = 0; face < element_count; ++face) {
                        const size_t i = static_cast<size_t>(face) * 3u;
                        const uint32_t ia = mesh.indices[i + 0u];
                        const uint32_t ib = mesh.indices[i + 1u];
                        const uint32_t ic = mesh.indices[i + 2u];
                        if (ia >= vertex_count
                            || ib >= vertex_count
                            || ic >= vertex_count)
                        {
                            continue;
                        }
                        const float area =
                            mesh_triangle_area(
                                mesh.vertices[ia],
                                mesh.vertices[ib],
                                mesh.vertices[ic]);
                        if (area <= kAreaEpsilon) {
                            values[face] = 0.0f;
                            continue;
                        }
                        const float inverse_area = 1.0f / area;
                        if (desc.source_kind
                            == BuiltinMeshDerivedFieldSourceKind::
                                InverseAreaDensity)
                        {
                            values[face] = inverse_area;
                        }
                        else {
                            values[face] = std::log(inverse_area);
                            if (!std::isfinite(values[face])) {
                                values[face] = 0.0f;
                            }
                        }
                    }
                    if (desc.normalize) {
                        normalize_by_max_positive(values);
                    }
                    return true;
                }

                std::vector<float> vertex_areas(vertex_count, 0.0f);
                for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
                    const uint32_t ia = mesh.indices[i + 0u];
                    const uint32_t ib = mesh.indices[i + 1u];
                    const uint32_t ic = mesh.indices[i + 2u];
                    if (ia >= vertex_count
                        || ib >= vertex_count
                        || ic >= vertex_count)
                    {
                        continue;
                    }
                    const float area =
                        mesh_triangle_area(
                            mesh.vertices[ia],
                            mesh.vertices[ib],
                            mesh.vertices[ic])
                        / 3.0f;
                    vertex_areas[ia] += area;
                    vertex_areas[ib] += area;
                    vertex_areas[ic] += area;
                }

                for (uint32_t i = 0; i < vertex_count; ++i) {
                    if (vertex_areas[i] <= kAreaEpsilon) {
                        values[i] = 0.0f;
                        continue;
                    }

                    const float inverse_area = 1.0f / vertex_areas[i];
                    if (desc.source_kind
                        == BuiltinMeshDerivedFieldSourceKind::
                            InverseAreaDensity)
                    {
                        values[i] = inverse_area;
                    }
                    else {
                        values[i] = std::log(inverse_area);
                        if (!std::isfinite(values[i])) {
                            values[i] = 0.0f;
                        }
                    }
                }
                if (desc.normalize) {
                    normalize_by_max_positive(values);
                }
                return true;
            }
            }

            logger.error("builtin mesh derived field source kind is invalid");
            return false;
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

        bool cached_compute_field_matches_source(
            const MeshDerivedFieldData& field,
            const MeshComputeDerivedFieldDesc& desc,
            const MeshData& source_mesh) noexcept
        {
            if (!field.valid()
                || field.source_mesh_key != desc.source_mesh.output
                || field.source_topology_hash
                    != compute_mesh_topology_hash(source_mesh)
                || field.domain != desc.domain
                || field.element_count
                    != mesh_domain_element_count(source_mesh, desc.domain)
                || field.channels.size() != desc.channels.size())
            {
                return false;
            }

            uint32_t expected_offset = 0u;
            for (size_t i = 0; i < desc.channels.size(); ++i) {
                const MeshDerivedFieldChannel& channel = field.channels[i];
                const uint32_t expected_bytes =
                    field.element_count
                    * mesh_derived_field_value_stride(
                        desc.channels[i].value_type);
                if (channel.channel_id != desc.channels[i].channel_id
                    || channel.value_type != desc.channels[i].value_type
                    || channel.byte_offset != expected_offset
                    || channel.byte_count != expected_bytes)
                {
                    return false;
                }
                expected_offset += expected_bytes;
            }
            return true;
        }

        wz::asset::AssetNode compile_mesh_compute_derived_field_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshFieldComputeBackend& mesh_field_compute,
            MeshTable& mesh_table,
            ComputePipelineTable& compute_pipeline_table,
            MeshDerivedFieldTable& field_table,
            GpuResidentFieldTable& gpu_resident_field_table,
            GpuResidentMeshDataTable& gpu_resident_mesh_data_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            MeshComputeDerivedFieldDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshComputeDerivedFieldDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        compute_derived_field_desc_from_params(
                            *params,
                            dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.size() != 2u) {
                logger.error(
                    "mesh compute derived field node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error(
                    "mesh compute derived field source mesh handle invalid");
                return compile_failed_node(input);
            }

            MeshDerivedFieldData cached{};
            if (load_cached_mesh_derived_field_with_key(
                    cache_settings,
                    kMeshComputeDerivedFieldDiskCacheKey,
                    input.key,
                    kMeshComputeDerivedFieldCompilerVersion,
                    logger,
                    cached))
            {
                if (cached_compute_field_matches_source(
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
                logger.warn(
                    "asset disk cache ignored stale mesh compute derived field");
            }

            const ComputePipelineData* compute_pipeline =
                compute_pipeline_table.get(dep_handles[1]);
            if (!compute_pipeline) {
                logger.error(
                    "mesh compute derived field compute pipeline dependency invalid");
                return compile_failed_node(input);
            }

            MeshDerivedFieldData field{};
            if (!compile_mesh_compute_derived_field(
                    input.key,
                    *desc,
                    *source_mesh,
                    *compute_pipeline,
                    mesh_field_compute,
                    logger,
                    gpu_resident_field_table,
                    gpu_resident_mesh_data_table,
                    field))
            {
                return compile_failed_node(input);
            }

            store_cached_mesh_derived_field(
                cache_settings,
                kMeshComputeDerivedFieldDiskCacheKey,
                input.key,
                kMeshComputeDerivedFieldCompilerVersion,
                field,
                logger);

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_explicit_mesh_derived_field_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            ExplicitMeshDerivedFieldDesc param_desc{};
            const auto* desc =
                std::any_cast<ExplicitMeshDerivedFieldDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        explicit_field_desc_from_params(*params, dep_nodes);
                    desc = &param_desc;
                }
            }
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

        wz::asset::AssetNode compile_mesh_sparse_apply_field_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table,
            MeshSparseOperatorTable& sparse_operator_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            MeshSparseApplyFieldDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshSparseApplyFieldDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        sparse_apply_field_desc_from_params(
                            *params,
                            dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.size() != 3u) {
                logger.error("mesh sparse apply field node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error("mesh sparse apply source mesh handle invalid");
                return compile_failed_node(input);
            }

            const MeshDerivedFieldData* input_field =
                field_table.get(dep_handles[1]);
            if (!input_field) {
                logger.error("mesh sparse apply input field handle invalid");
                return compile_failed_node(input);
            }

            const MeshSparseOperatorData* sparse_operator =
                sparse_operator_table.get(dep_handles[2]);
            if (!sparse_operator) {
                logger.error("mesh sparse apply operator handle invalid");
                return compile_failed_node(input);
            }

            MeshDerivedFieldData cached{};
            if (load_cached_mesh_derived_field_with_key(
                    cache_settings,
                    kMeshDerivedFieldDiskCacheKey,
                    input.key,
                    kMeshSparseApplyFieldCompilerVersion,
                    logger,
                    cached))
            {
                if (cached_sparse_apply_field_matches_source(
                        cached,
                        *desc,
                        *source_mesh,
                        *input_field,
                        *sparse_operator))
                {
                    const wz::asset::ResourceHandle handle =
                        field_table.add(std::move(cached));
                    return handle.valid()
                        ? compiled_field_node(input, handle)
                        : compile_failed_node(input);
                }
                logger.warn(
                    "asset disk cache ignored stale mesh sparse apply field");
            }

            MeshDerivedFieldData field{};
            if (!compile_mesh_sparse_apply_field(
                    *desc,
                    *source_mesh,
                    *input_field,
                    *sparse_operator,
                    logger,
                    field))
            {
                return compile_failed_node(input);
            }

            store_cached_mesh_derived_field(
                cache_settings,
                kMeshDerivedFieldDiskCacheKey,
                input.key,
                kMeshSparseApplyFieldCompilerVersion,
                field,
                logger);

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_mesh_sparse_diffusion_bands_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table,
            MeshSparseOperatorTable& sparse_operator_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            MeshSparseDiffusionBandsDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshSparseDiffusionBandsDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        sparse_diffusion_bands_desc_from_params(
                            *params,
                            dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.size() != 3u) {
                logger.error(
                    "mesh sparse diffusion bands node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error(
                    "mesh sparse diffusion bands source mesh handle invalid");
                return compile_failed_node(input);
            }

            const MeshDerivedFieldData* input_field =
                field_table.get(dep_handles[1]);
            if (!input_field) {
                logger.error(
                    "mesh sparse diffusion bands input field handle invalid");
                return compile_failed_node(input);
            }

            const MeshSparseOperatorData* sparse_operator =
                sparse_operator_table.get(dep_handles[2]);
            if (!sparse_operator) {
                logger.error(
                    "mesh sparse diffusion bands operator handle invalid");
                return compile_failed_node(input);
            }

            MeshDerivedFieldData cached{};
            if (load_cached_mesh_derived_field_with_key(
                    cache_settings,
                    kMeshDerivedFieldDiskCacheKey,
                    input.key,
                    kMeshSparseDiffusionBandsCompilerVersion,
                    logger,
                    cached))
            {
                if (cached_sparse_diffusion_bands_matches_source(
                        cached,
                        *desc,
                        *source_mesh,
                        *input_field,
                        *sparse_operator))
                {
                    const wz::asset::ResourceHandle handle =
                        field_table.add(std::move(cached));
                    return handle.valid()
                        ? compiled_field_node(input, handle)
                        : compile_failed_node(input);
                }
                logger.warn(
                    "asset disk cache ignored stale mesh sparse diffusion bands");
            }

            MeshDerivedFieldData field{};
            if (!compile_mesh_sparse_diffusion_bands(
                    *desc,
                    *source_mesh,
                    *input_field,
                    *sparse_operator,
                    logger,
                    field))
            {
                return compile_failed_node(input);
            }

            store_cached_mesh_derived_field(
                cache_settings,
                kMeshDerivedFieldDiskCacheKey,
                input.key,
                kMeshSparseDiffusionBandsCompilerVersion,
                field,
                logger);

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_mesh_field_level_mask_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            MeshFieldLevelMaskDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshFieldLevelMaskDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        field_level_mask_desc_from_params(
                            *params,
                            dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.size() != 2u) {
                logger.error("mesh field level mask node missing desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error(
                    "mesh field level mask source mesh handle invalid");
                return compile_failed_node(input);
            }

            const MeshDerivedFieldData* input_field =
                field_table.get(dep_handles[1]);
            if (!input_field) {
                logger.error(
                    "mesh field level mask input field handle invalid");
                return compile_failed_node(input);
            }

            MeshDerivedFieldData cached{};
            if (load_cached_mesh_derived_field_with_key(
                    cache_settings,
                    kMeshDerivedFieldDiskCacheKey,
                    input.key,
                    kMeshFieldLevelMaskCompilerVersion,
                    logger,
                    cached))
            {
                if (cached_field_level_mask_matches_source(
                        cached,
                        *desc,
                        *source_mesh,
                        *input_field))
                {
                    const wz::asset::ResourceHandle handle =
                        field_table.add(std::move(cached));
                    return handle.valid()
                        ? compiled_field_node(input, handle)
                        : compile_failed_node(input);
                }
                logger.warn(
                    "asset disk cache ignored stale mesh field level mask");
            }

            MeshDerivedFieldData field{};
            if (!compile_mesh_field_level_mask(
                    *desc,
                    *source_mesh,
                    *input_field,
                    logger,
                    field))
            {
                return compile_failed_node(input);
            }

            store_cached_mesh_derived_field(
                cache_settings,
                kMeshDerivedFieldDiskCacheKey,
                input.key,
                kMeshFieldLevelMaskCompilerVersion,
                field,
                logger);

            const wz::asset::ResourceHandle handle =
                field_table.add(std::move(field));
            return handle.valid()
                ? compiled_field_node(input, handle)
                : compile_failed_node(input);
        }

        wz::asset::AssetNode compile_builtin_mesh_derived_field_node(
            const wz::asset::AssetNode& input,
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            BuiltinMeshDerivedFieldDesc param_desc{};
            const auto* desc =
                std::any_cast<BuiltinMeshDerivedFieldDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        builtin_field_desc_from_params(*params, dep_nodes);
                    desc = &param_desc;
                }
            }
            if (!desc || dep_handles.size() != 1u) {
                logger.error(
                    "mesh derived field node missing builtin source desc");
                return compile_failed_node(input);
            }

            const MeshData* source_mesh = mesh_table.get(dep_handles[0]);
            if (!source_mesh) {
                logger.error(
                    "builtin mesh derived field source mesh handle invalid");
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
                if (cached_builtin_field_matches_source(
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
                logger.warn(
                    "asset disk cache ignored stale builtin mesh derived field");
            }

            std::vector<float> values;
            if (!build_builtin_mesh_derived_field_values(
                    *desc,
                    *source_mesh,
                    values,
                    logger))
            {
                return compile_failed_node(input);
            }

            const ExplicitMeshDerivedFieldDesc explicit_desc{
                .name = desc->name,
                .source_mesh = desc->source_mesh,
                .domain = desc->domain,
                .element_count =
                    mesh_domain_element_count(*source_mesh, desc->domain),
                .channels = {
                    MeshDerivedFieldChannelDesc{
                        .channel_id = desc->channel_id,
                        .value_type = desc->value_type,
                        .values = float_channel_bytes(values),
                    },
                },
            };

            MeshDerivedFieldData field{};
            if (!pack_explicit_field(
                    input.key,
                    explicit_desc,
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
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshFieldComputeBackend& mesh_field_compute,
            MeshTable& mesh_table,
            ComputePipelineTable& compute_pipeline_table,
            MeshDerivedFieldTable& field_table,
            GpuResidentFieldTable& gpu_resident_field_table,
            const EngineAssetCacheSettings& cache_settings)
        {
            MeshWaveletAnalysisDesc param_desc{};
            const auto* desc =
                std::any_cast<MeshWaveletAnalysisDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        wavelet_analysis_desc_from_params(*params, dep_nodes);
                    desc = &param_desc;
                }
            }
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
            std::span<const wz::asset::AssetNode> dep_nodes,
            std::span<const wz::asset::ResourceHandle> dep_handles,
            wz::Logger& logger,
            MeshTable& mesh_table,
            MeshDerivedFieldTable& field_table)
        {
            BehaviorFieldPlaceholderDesc param_desc{};
            const auto* desc =
                std::any_cast<BehaviorFieldPlaceholderDesc>(&input.meta);
            if (!desc) {
                if (const auto* params =
                        std::any_cast<wz::asset::ParamBlock>(&input.meta))
                {
                    param_desc =
                        behavior_placeholder_desc_from_params(
                            *params,
                            dep_nodes);
                    desc = &param_desc;
                }
            }
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
        MeshSparseOperatorTable& mesh_sparse_operator_table,
        GpuResidentFieldTable& gpu_resident_field_table,
        GpuResidentMeshDataTable& gpu_resident_mesh_data_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshDerivedFieldExplicitSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(MeshDerivedFieldDomain::Vertex),
                    .options = kDomainOptions,
                },
                {
                    .name = "element_count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Element count",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 10000000.0,
                },
                {
                    .name = "channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Channel id",
                    .default_num = 8192.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
                {
                    .name = "value_type",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Value type",
                    .default_num =
                        static_cast<double>(
                            MeshDerivedFieldValueType::Float1),
                    .options = kValueTypeOptions,
                },
            },
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_explicit_mesh_derived_field_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table,
                    cache_settings);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kBuiltinMeshDerivedFieldSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(MeshDerivedFieldDomain::Vertex),
                    .options = kDomainOptions,
                },
                {
                    .name = "channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Channel id",
                    .default_num = 8192.0,
                    .min = 1.0,
                    .max = 65535.0,
                },
                {
                    .name = "value_type",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Value type",
                    .default_num =
                        static_cast<double>(
                            MeshDerivedFieldValueType::Float1),
                    .options = kValueTypeOptions,
                },
                {
                    .name = "source_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Source kind",
                    .default_num =
                        static_cast<double>(
                            BuiltinMeshDerivedFieldSourceKind::
                                PositionGradient),
                    .options = kBuiltinSourceOptions,
                },
                {
                    .name = "component",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Component",
                    .default_num =
                        static_cast<double>(
                            BuiltinMeshDerivedFieldComponent::Y),
                    .options = kBuiltinComponentOptions,
                },
                {
                    .name = "normalize",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Normalize",
                    .default_num = 1.0,
                },
                {
                    .name = "constant_value",
                    .type = wz::asset::ParamType::Float,
                    .label = "Constant value",
                    .default_num = 0.5,
                    .min = -1000000.0,
                    .max = 1000000.0,
                },
            },
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_builtin_mesh_derived_field_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table,
                    cache_settings);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshSparseApplyFieldSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
                { "source_field", kAssetTypeMeshDerivedField },
                { "operator", kAssetTypeMeshSparseOperator },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "input_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Input channel id",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
                {
                    .name = "output_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Output channel id",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
                {
                    .name = "apply_mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Apply mode",
                    .default_num =
                        static_cast<double>(MeshSparseApplyMode::Residual),
                    .options = kSparseApplyModeOptions,
                },
            },
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                &mesh_sparse_operator_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_sparse_apply_field_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table,
                    mesh_sparse_operator_table,
                    cache_settings);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshSparseDiffusionBandsSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
                { "source_field", kAssetTypeMeshDerivedField },
                { "operator", kAssetTypeMeshSparseOperator },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "input_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Input channel id",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
                {
                    .name = "output_base_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Output base channel id",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
                {
                    .name = "band_count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Band count",
                    .default_num = 1.0,
                    .min = 1.0,
                    .max = 64.0,
                },
                {
                    .name = "iterations_per_band",
                    .type = wz::asset::ParamType::Int,
                    .label = "Iterations per band",
                    .default_num = 1.0,
                    .min = 1.0,
                    .max = 4096.0,
                },
                {
                    .name = "mode",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Mode",
                    .default_num =
                        static_cast<double>(MeshSparseDiffusionMode::Smooth),
                    .options = kSparseDiffusionModeOptions,
                },
                {
                    .name = "tau",
                    .type = wz::asset::ParamType::Float,
                    .label = "Tau",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1000.0,
                },
            },
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                &mesh_sparse_operator_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_sparse_diffusion_bands_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    mesh_table,
                    mesh_derived_field_table,
                    mesh_sparse_operator_table,
                    cache_settings);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kMeshFieldLevelMaskSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
                { "source_field", kAssetTypeMeshDerivedField },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(MeshDerivedFieldDomain::Face),
                    .options = kDomainOptions,
                },
                {
                    .name = "input_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Input channel id",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
                {
                    .name = "output_channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Output channel id",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
                {
                    .name = "min_value",
                    .type = wz::asset::ParamType::Float,
                    .label = "Min value",
                    .default_num = 0.0,
                },
                {
                    .name = "max_value",
                    .type = wz::asset::ParamType::Float,
                    .label = "Max value",
                    .default_num = 1.0,
                },
            },
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_field_level_mask_node(
                    input,
                    dep_nodes,
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
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
                { "compute_pipeline", kAssetTypeComputePipeline },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "scale_count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Scale count",
                    .default_num = 3.0,
                    .min = 1.0,
                    .max = 32.0,
                },
                {
                    .name = "lambda_max_estimate",
                    .type = wz::asset::ParamType::Float,
                    .label = "Lambda max estimate",
                    .default_num = 2.0,
                    .min = 0.0001,
                    .max = 1000.0,
                },
                {
                    .name = "gamma",
                    .type = wz::asset::ParamType::Float,
                    .label = "Gamma",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100.0,
                },
            },
            .compile = [
                &logger,
                &mesh_field_compute,
                &mesh_table,
                &compute_pipeline_table,
                &mesh_derived_field_table,
                &gpu_resident_field_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_wavelet_analysis_node(
                    input,
                    dep_nodes,
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
            .input_schema = kMeshComputeDerivedFieldSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
                { "compute_pipeline", kAssetTypeComputePipeline },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(MeshDerivedFieldDomain::Vertex),
                    .options = kDomainOptions,
                },
                {
                    .name = "channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Channel id",
                    .default_num = 8192.0,
                    .min = 1.0,
                    .max = 65535.0,
                },
                {
                    .name = "value_type",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Value type",
                    .default_num =
                        static_cast<double>(
                            MeshDerivedFieldValueType::Float1),
                    .options = kValueTypeOptions,
                },
            },
            .compile = [
                &logger,
                &mesh_field_compute,
                &mesh_table,
                &compute_pipeline_table,
                &mesh_derived_field_table,
                &gpu_resident_field_table,
                &gpu_resident_mesh_data_table,
                cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_mesh_compute_derived_field_node(
                    input,
                    dep_nodes,
                    dep_handles,
                    logger,
                    mesh_field_compute,
                    mesh_table,
                    compute_pipeline_table,
                    mesh_derived_field_table,
                    gpu_resident_field_table,
                    gpu_resident_mesh_data_table,
                    cache_settings);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kBehaviorFieldPlaceholderSchema,
            .output_type = kAssetTypeMeshDerivedField,
            .input_ports = {
                { "source_mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "domain",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(MeshDerivedFieldDomain::Vertex),
                    .options = kDomainOptions,
                },
                {
                    .name = "channel_id",
                    .type = wz::asset::ParamType::Int,
                    .label = "Channel id",
                    .default_num = 0.0,
                    .min = 0.0,
                    .max = 65535.0,
                },
            },
            .compile = [
                &logger,
                &mesh_table,
                &mesh_derived_field_table](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                return compile_behavior_field_placeholder_node(
                    input,
                    dep_nodes,
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

    bool load_cached_mesh_compute_derived_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        MeshDerivedFieldData& field)
    {
        return load_cached_mesh_derived_field_with_key(
            cache,
            kMeshComputeDerivedFieldDiskCacheKey,
            key,
            kMeshComputeDerivedFieldCompilerVersion,
            logger,
            field);
    }
}
