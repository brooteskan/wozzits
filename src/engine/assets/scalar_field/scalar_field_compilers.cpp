// src/engine/assets/scalar_field/scalar_field_compilers.cpp

#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <file/filesystem.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kScalarFieldDiskCacheMagic = 0x53465a57u;
        constexpr uint32_t kScalarFieldDiskCacheVersion = 1u;

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

        wz::fs::Path scalar_field_cache_directory(
            const EngineAssetCacheSettings& cache)
        {
            return disk_cache_asset_directory(
                cache,
                kScalarFieldDiskCacheKey.subdirectory);
        }

        wz::fs::Path scalar_field_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return disk_cache_asset_path(
                cache,
                kScalarFieldDiskCacheKey.subdirectory,
                key,
                kScalarFieldDiskCacheKey.seed_lo,
                kScalarFieldDiskCacheKey.seed_hi);
        }

        std::vector<uint8_t> serialize_scalar_field_asset(
            const wz::asset::AssetKey& key,
            const ScalarFieldData& field)
        {
            std::vector<uint8_t> out;
            out.reserve(
                128u + field.values.size() * sizeof(float));
            append_scalar(out, kScalarFieldDiskCacheMagic);
            append_scalar(out, kScalarFieldDiskCacheVersion);
            append_scalar(out, kScalarFieldCompilerVersion);
            append_asset_key(out, key);
            append_scalar(out, field.width);
            append_scalar(out, field.height);
            append_scalar(out, field.depth);
            append_scalar(out, static_cast<uint8_t>(field.format));
            append_scalar(out, static_cast<uint8_t>(field.domain_kind));
            append_scalar(out, static_cast<uint8_t>(field.layout));
            append_scalar(out, static_cast<uint8_t>(field.origin));
            append_scalar(out, field.min_value);
            append_scalar(out, field.max_value);
            append_scalar(out, static_cast<uint64_t>(field.values.size()));
            append_raw_bytes(
                out,
                field.values.data(),
                field.values.size() * sizeof(float));
            return out;
        }

        bool deserialize_scalar_field_asset(
            const std::vector<uint8_t>& bytes,
            const wz::asset::AssetKey& expected_key,
            ScalarFieldData& field)
        {
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kScalarFieldDiskCacheMagic
                || version != kScalarFieldDiskCacheVersion
                || compiler_version != kScalarFieldCompilerVersion)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            uint8_t format = 0;
            uint8_t domain_kind = 0;
            uint8_t layout = 0;
            uint8_t origin = 0;
            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key
                || !read_scalar(bytes, offset, field.width)
                || !read_scalar(bytes, offset, field.height)
                || !read_scalar(bytes, offset, field.depth)
                || !read_scalar(bytes, offset, format)
                || !read_scalar(bytes, offset, domain_kind)
                || !read_scalar(bytes, offset, layout)
                || !read_scalar(bytes, offset, origin)
                || !read_scalar(bytes, offset, field.min_value)
                || !read_scalar(bytes, offset, field.max_value))
            {
                return false;
            }

            field.format = static_cast<ScalarFieldFormat>(format);
            field.domain_kind =
                static_cast<ScalarFieldDomainKind>(domain_kind);
            field.layout = static_cast<ScalarFieldSampleLayout>(layout);
            field.origin = static_cast<ScalarFieldOrigin>(origin);

            uint64_t count = 0;
            if (!read_scalar(bytes, offset, count)) {
                return false;
            }
            const uint64_t remaining =
                static_cast<uint64_t>(bytes.size() - offset);
            if (count > remaining / sizeof(float)) {
                return false;
            }
            field.values.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    field.values.data(),
                    field.values.size() * sizeof(float)))
            {
                return false;
            }
            return offset == bytes.size() && field.valid();
        }

        bool load_cached_scalar_field_impl(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            wz::Logger& logger,
            ScalarFieldData& field)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }

            const wz::fs::Path path = scalar_field_cache_path(cache, key);
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = wz::fs::read_file(path);
            if (!bytes) {
                logger.info("asset disk cache miss: scalar field " + path);
                return false;
            }

            ScalarFieldData loaded{};
            if (!deserialize_scalar_field_asset(bytes.value, key, loaded)) {
                logger.warn(
                    "asset disk cache ignored invalid scalar field: " + path);
                return false;
            }

            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            field = std::move(loaded);
            logger.info(
                "asset disk cache hit: scalar field "
                + path
                + " ms="
                + std::to_string(elapsed));
            return true;
        }

        void store_cached_scalar_field(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const ScalarFieldData& field,
            wz::Logger& logger)
        {
            if (!cache.enabled || cache.root.empty() || !field.valid()) {
                return;
            }

            const wz::fs::Path directory = scalar_field_cache_directory(cache);
            if (wz::fs::create_directories(directory)
                != wz::fs::FileError::None)
            {
                logger.warn(
                    "asset disk cache directory unavailable: " + directory);
                return;
            }

            const wz::fs::Path path = scalar_field_cache_path(cache, key);
            const std::vector<uint8_t> bytes =
                serialize_scalar_field_asset(key, field);
            const wz::fs::FileError err = wz::fs::write_file(path, bytes, true);
            if (err != wz::fs::FileError::None) {
                logger.warn(
                    "asset disk cache write failed: scalar field "
                    + path
                    + " error="
                    + std::to_string(static_cast<int>(err)));
                return;
            }
            logger.info(
                "asset disk cache stored: scalar field "
                + path
                + " bytes="
                + std::to_string(bytes.size()));
        }

        // Validates all values (rejects NaN and infinity) and computes
        // min_value / max_value. Returns false and logs on the first bad value.
        bool compute_min_max(
            const std::vector<float>& values,
            float& min_value,
            float& max_value,
            wz::Logger& logger,
            std::string_view label)
        {
            if (values.empty()) {
                logger.error(std::string(label) + " has no values");
                return false;
            }

            min_value = std::numeric_limits<float>::max();
            max_value = std::numeric_limits<float>::lowest();

            for (uint32_t i = 0; i < static_cast<uint32_t>(values.size()); ++i) {
                const float v = values[i];

                if (std::isnan(v)) {
                    logger.error(std::string(label)
                        + " contains NaN at sample index " + std::to_string(i));
                    return false;
                }

                if (std::isinf(v)) {
                    logger.error(std::string(label)
                        + " contains infinity at sample index " + std::to_string(i));
                    return false;
                }

                if (v < min_value) min_value = v;
                if (v > max_value) max_value = v;
            }

            return true;
        }
    }


    void register_scalar_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        const EngineAssetCacheSettings& cache_settings)
    {
        // ── Scalar field compiler (file-backed) ───────────────────────────────
        //
        // Dispatches on kScalarFieldFromRawF32Schema.
        // Expects exactly one dependency: a kRawFileSchema node whose compiled
        // payload is a std::vector<uint8_t> of raw float32 bytes.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldFromRawF32Schema,
            .output_type = kAssetTypeScalarField,
            .compile = [&logger, &scalar_field_table, cache_settings](
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

                ScalarFieldData cached_data{};
                if (load_cached_scalar_field_impl(
                        cache_settings,
                        input.key,
                        logger,
                        cached_data))
                {
                    wz::asset::ResourceHandle handle =
                        scalar_field_table.add(std::move(cached_data));
                    wz::asset::AssetNode out = input;
                    out.stage = wz::asset::AssetStage::Compiled;
                    out.payload = handle;
                    return out;
                }

                std::vector<float> values(count);
                std::memcpy(values.data(), bytes->data(), expected_bytes);

                // ── 5. Validate values; compute min/max ───────────────────────

                float min_val = 0.0f;
                float max_val = 0.0f;

                if (!compute_min_max(values, min_val, max_val,
                                     logger, "scalar field"))
                {
                    return compile_failed_node(input);
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

                store_cached_scalar_field(
                    cache_settings,
                    input.key,
                    data,
                    logger);

                wz::asset::ResourceHandle handle =
                    scalar_field_table.add(std::move(data));

                // ── 7. Return compiled node ───────────────────────────────────

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });


        // ── Scalar field compiler (procedural) ────────────────────────────────
        //
        // Dispatches on kScalarFieldProceduralSchema.
        // Generates values from ProceduralScalarFieldCompileDesc metadata alone;
        // expects no file dependencies in dep_nodes.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldProceduralSchema,
            .output_type = kAssetTypeScalarField,
            .compile = [&logger, &scalar_field_table, cache_settings](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate metadata ──────────────────────────────────────

                const auto* desc =
                    std::any_cast<ProceduralScalarFieldCompileDesc>(&input.meta);

                if (!desc) {
                    logger.error("procedural scalar field node missing "
                        "ProceduralScalarFieldCompileDesc");
                    return compile_failed_node(input);
                }

                if (desc->width == 0 || desc->height == 0 || desc->depth == 0) {
                    logger.error("procedural scalar field has zero dimension ("
                        + std::to_string(desc->width) + "x"
                        + std::to_string(desc->height) + "x"
                        + std::to_string(desc->depth) + ")");
                    return compile_failed_node(input);
                }

                if (desc->depth != 1) {
                    logger.error(
                        "procedural scalar field depth > 1 is not supported in V1 "
                        "(got depth=" + std::to_string(desc->depth) + ")");
                    return compile_failed_node(input);
                }

                if (desc->format != ScalarFieldFormat::Float32) {
                    logger.error(
                        "procedural scalar field only supports Float32 in V1");
                    return compile_failed_node(input);
                }

                if (!dep_nodes.empty()) {
                    logger.error(
                        "procedural scalar field should not have dependencies");
                    return compile_failed_node(input);
                }

                // ── 2. Generate values ────────────────────────────────────────

                const uint32_t width = desc->width;
                const uint32_t height = desc->height;
                const uint32_t count = width * height; // depth == 1

                ScalarFieldData cached_data{};
                if (load_cached_scalar_field_impl(
                        cache_settings,
                        input.key,
                        logger,
                        cached_data))
                {
                    wz::asset::ResourceHandle handle =
                        scalar_field_table.add(std::move(cached_data));
                    wz::asset::AssetNode out = input;
                    out.stage = wz::asset::AssetStage::Compiled;
                    out.payload = handle;
                    return out;
                }

                std::vector<float> values(count);

                constexpr float pi = 3.14159265358979323846f;

                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        float value = 0.0f;

                        switch (desc->generator) {

                        case ScalarFieldGenerator::GradientX:
                            value = (width > 1)
                                ? static_cast<float>(x) /
                                  static_cast<float>(width - 1)
                                : 0.0f;
                            break;

                        case ScalarFieldGenerator::GradientY:
                            value = (height > 1)
                                ? static_cast<float>(y) /
                                  static_cast<float>(height - 1)
                                : 0.0f;
                            break;

                        case ScalarFieldGenerator::RadialGradient:
                        {
                            const float cx = 0.5f * static_cast<float>(width - 1);
                            const float cy = 0.5f * static_cast<float>(height - 1);
                            const float dx = static_cast<float>(x) - cx;
                            const float dy = static_cast<float>(y) - cy;
                            const float max_dist =
                                std::sqrt(cx * cx + cy * cy);
                            const float dist =
                                std::sqrt(dx * dx + dy * dy);
                            value = (max_dist > 0.0f)
                                ? std::min(dist / max_dist, 1.0f)
                                : 0.0f;
                            value *= desc->amplitude;
                            break;
                        }

                        case ScalarFieldGenerator::Checkerboard:
                        {
                            const uint32_t cell =
                                std::max(1u, static_cast<uint32_t>(
                                    desc->frequency));
                            value = ((x / cell + y / cell) % 2 != 0)
                                ? desc->amplitude
                                : 0.0f;
                            break;
                        }

                        case ScalarFieldGenerator::SineWaves:
                        {
                            // u in [0, 1] across width; guard for width == 1.
                            const float u = (width > 1)
                                ? static_cast<float>(x) /
                                  static_cast<float>(width - 1)
                                : 0.0f;
                            // For amplitude == 1, output is [0, 1].
                            // Larger amplitudes intentionally produce values
                            // outside [0, 1]; min_value/max_value record the
                            // true generated range.
                            value = (0.5f + 0.5f * std::sin(
                                u * desc->frequency * 2.0f * pi))
                                * desc->amplitude;
                            break;
                        }

                        } // switch

                        values[x + y * width] = value;
                    }
                }

                // ── 3. Validate values; compute min/max ───────────────────────

                float min_val = 0.0f;
                float max_val = 0.0f;

                if (!compute_min_max(values, min_val, max_val,
                                     logger, "procedural scalar field"))
                {
                    return compile_failed_node(input);
                }

                // ── 4. Store in ScalarFieldTable ──────────────────────────────

                ScalarFieldData data;
                data.width = desc->width;
                data.height = desc->height;
                data.depth = desc->depth;
                data.format = desc->format;
                data.domain_kind = desc->domain_kind;
                data.min_value = min_val;
                data.max_value = max_val;
                data.values = std::move(values);

                store_cached_scalar_field(
                    cache_settings,
                    input.key,
                    data,
                    logger);

                wz::asset::ResourceHandle handle =
                    scalar_field_table.add(std::move(data));

                // ── 5. Return compiled node ───────────────────────────────────

                wz::asset::AssetNode out = input;
                out.stage = wz::asset::AssetStage::Compiled;
                out.payload = handle;
                return out;
            }
            });
    }

    bool load_cached_scalar_field(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        ScalarFieldData& field)
    {
        return load_cached_scalar_field_impl(cache, key, logger, field);
    }

} // namespace wz::engine::assets::internal
