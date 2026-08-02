// src/engine/assets/scalar_field/scalar_field_compilers.cpp

#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>
#include <file/filesystem.h>

#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/gpu_resource_registry.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kScalarFieldDiskCacheMagic = 0x53465a57u;
        constexpr uint32_t kScalarFieldDiskCacheVersion = 1u;

        constexpr std::array<std::string_view, 6>
            kScalarFieldDomainOptions = {
                "Unknown",
                "Spatial 1D",
                "Spatial 2D",
                "Lookup 1D",
                "Lookup 2D",
                "Baked computation",
            };

        constexpr std::array<std::string_view, 5>
            kScalarFieldGeneratorOptions = {
                "Gradient X",
                "Gradient Y",
                "Radial gradient",
                "Checkerboard",
                "Sine waves",
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

        ScalarFieldCompileDesc scalar_field_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            ScalarFieldCompileDesc desc{};
            desc.width = params.get<uint32_t>("width", desc.width);
            desc.height = params.get<uint32_t>("height", desc.height);
            desc.depth = params.get<uint32_t>("depth", desc.depth);
            desc.domain_kind =
                enum_param(
                    params,
                    "domain_kind",
                    desc.domain_kind,
                    kScalarFieldDomainOptions);
            return desc;
        }

        ProceduralScalarFieldCompileDesc
        procedural_scalar_field_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            ProceduralScalarFieldCompileDesc desc{};
            desc.width = params.get<uint32_t>("width", desc.width);
            desc.height = params.get<uint32_t>("height", desc.height);
            desc.depth = params.get<uint32_t>("depth", desc.depth);
            desc.generator =
                enum_param(
                    params,
                    "generator",
                    desc.generator,
                    kScalarFieldGeneratorOptions);
            desc.frequency =
                params.get<float>("frequency", desc.frequency);
            desc.amplitude =
                params.get<float>("amplitude", desc.amplitude);
            desc.domain_kind =
                enum_param(
                    params,
                    "domain_kind",
                    desc.domain_kind,
                    kScalarFieldDomainOptions);
            return desc;
        }

        TerrainScalarFieldCompileDesc
        terrain_scalar_field_desc_from_params(
            const wz::asset::ParamBlock& params)
        {
            TerrainScalarFieldCompileDesc desc{};
            desc.resolution =
                params.get<uint32_t>("resolution", desc.resolution);
            desc.ridge_count =
                params.get<float>("ridge_count", desc.ridge_count);
            desc.ridginess =
                params.get<float>("ridginess", desc.ridginess);
            desc.roughness =
                params.get<float>("roughness", desc.roughness);
            desc.detail = params.get<uint32_t>("detail", desc.detail);
            desc.seed = params.get<uint32_t>("seed", desc.seed);
            desc.basin_radius =
                params.get<float>("basin_radius", desc.basin_radius);
            desc.basin_falloff =
                params.get<float>("basin_falloff", desc.basin_falloff);
            desc.basin_depth =
                params.get<float>("basin_depth", desc.basin_depth);
            desc.domain_kind =
                enum_param(
                    params,
                    "domain_kind",
                    desc.domain_kind,
                    kScalarFieldDomainOptions);
            return desc;
        }


        // ─── Terrain noise ────────────────────────────────────────────────────
        //
        // Gradient (Perlin-style) noise with a hash-derived lattice, so the field
        // is fully determined by (x, y, seed) with no permutation table to ship
        // or to shuffle. Deterministic across runs and platforms: integer ops and
        // float arithmetic only, no RNG state, no std::random.

        // Murmur3 finaliser over the lattice coordinate and the seed.
        [[nodiscard]] uint32_t terrain_hash2(
            int32_t x, int32_t y, uint32_t seed) noexcept
        {
            uint32_t h = seed;
            h ^= static_cast<uint32_t>(x) * 0x8DA6B343u;
            h ^= static_cast<uint32_t>(y) * 0xD8163841u;
            h ^= h >> 16; h *= 0x85EBCA6Bu;
            h ^= h >> 13; h *= 0xC2B2AE35u;
            h ^= h >> 16;
            return h;
        }

        // Eight unit gradients. Perlin's improved-noise trick: a small fixed set
        // beats random directions, and the 45-degree diagonals keep it from
        // looking axis-aligned at the scales a silhouette is read at.
        constexpr float kTerrainGradients[8][2] = {
            {  1.0f,        0.0f       },
            { -1.0f,        0.0f       },
            {  0.0f,        1.0f       },
            {  0.0f,       -1.0f       },
            {  0.70710678f, 0.70710678f},
            { -0.70710678f, 0.70710678f},
            {  0.70710678f,-0.70710678f},
            { -0.70710678f,-0.70710678f},
        };

        // One octave. Returns roughly [-1, 1]: raw 2D gradient noise peaks at
        // +/-sqrt(2)/2, so the result is scaled by sqrt(2) to fill the range.
        [[nodiscard]] float terrain_noise2(
            float x, float y, uint32_t seed) noexcept
        {
            const float fx = std::floor(x);
            const float fy = std::floor(y);
            const auto  ix = static_cast<int32_t>(fx);
            const auto  iy = static_cast<int32_t>(fy);
            const float tx = x - fx;
            const float ty = y - fy;

            // Quintic fade: zero first AND second derivative at the lattice, so
            // octaves stack without creasing along the grid lines.
            const float u = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
            const float v = ty * ty * ty * (ty * (ty * 6.0f - 15.0f) + 10.0f);

            const auto corner = [&](int32_t cx, int32_t cy,
                                    float dx, float dy) -> float {
                const float* g =
                    kTerrainGradients[terrain_hash2(cx, cy, seed) & 7u];
                return g[0] * dx + g[1] * dy;
            };

            const float n00 = corner(ix,     iy,     tx,        ty       );
            const float n10 = corner(ix + 1, iy,     tx - 1.0f, ty       );
            const float n01 = corner(ix,     iy + 1, tx,        ty - 1.0f);
            const float n11 = corner(ix + 1, iy + 1, tx - 1.0f, ty - 1.0f);

            const float a = n00 + u * (n10 - n00);
            const float b = n01 + u * (n11 - n01);
            return (a + v * (b - a)) * 1.41421356f;
        }

        // Stacked octaves, returning the plain and ridged sums together so both
        // are paid for once. Both come back in [0, 1] so `ridginess` can blend
        // them without one range swamping the other.
        struct TerrainOctaveSums
        {
            float plain = 0.0f;    // fBm mapped to [0, 1]: rounded hills
            float ridged = 0.0f;   // 1 - |fBm|: sharp crests
        };

        [[nodiscard]] TerrainOctaveSums terrain_octaves(
            float x,
            float y,
            uint32_t octaves,
            float roughness,
            uint32_t seed) noexcept
        {
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float plain_sum = 0.0f;
            float ridged_sum = 0.0f;
            float norm = 0.0f;

            for (uint32_t o = 0; o < octaves; ++o) {
                // Decorrelate octaves by walking the seed, not the coordinate:
                // offsetting the position would make octaves share a lattice and
                // line their features up.
                const float n = terrain_noise2(
                    x * frequency,
                    y * frequency,
                    seed + o * 0x9E3779B9u);

                plain_sum += n * amplitude;
                // Simple 1 - |n| ridging. Not the classic weighted form (where
                // each octave is scaled by the previous one's value): that
                // produces prettier branching ridges but makes the dial
                // interact with `roughness` in a way an author cannot predict.
                ridged_sum += (1.0f - std::fabs(n)) * amplitude;
                norm += amplitude;

                amplitude *= roughness;
                frequency *= 2.0f;
            }

            const float inv = (norm > 0.0f) ? (1.0f / norm) : 0.0f;
            return TerrainOctaveSums{
                .plain  = 0.5f + 0.5f * (plain_sum * inv),
                .ridged = ridged_sum * inv,
            };
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

            if (!valid_scalar_field_format(format)
                || !valid_scalar_field_domain_kind(domain_kind)
                || !valid_scalar_field_sample_layout(layout)
                || !valid_scalar_field_origin(origin))
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

        // Box-filter one mip level down (#210). Produces mip level `dst` from the
        // already-built level `src`, given src's dimensions. Each destination
        // texel is the average of the 2x2 block of source texels it covers.
        //
        // Non-power-of-two edges: the destination dimension is max(src>>1, 1), so
        // when a source dimension is odd the last destination texel maps to a
        // single leftover source column/row (its 2x2 block is clamped to the
        // texels that actually exist) — average only the covered texels, never
        // read out of bounds. This is the standard "round-down + clamp" box
        // reduction; it matches how the hardware would generate these mips, so
        // GPU trilinear across levels stays consistent with our CPU chain.
        //
        // Row-major, index x + y*width, R32Float throughout.
        std::vector<float> box_filter_down(
            const std::vector<float>& src,
            uint32_t src_width,
            uint32_t src_height,
            uint32_t& dst_width,
            uint32_t& dst_height)
        {
            dst_width  = src_width  > 1u ? (src_width  >> 1u) : 1u;
            dst_height = src_height > 1u ? (src_height >> 1u) : 1u;

            std::vector<float> dst(
                static_cast<size_t>(dst_width) * dst_height);

            for (uint32_t dy = 0; dy < dst_height; ++dy) {
                const uint32_t sy0 = dy * 2u;
                const uint32_t sy1 = sy0 + 1u;
                const bool have_y1 = sy1 < src_height;
                for (uint32_t dx = 0; dx < dst_width; ++dx) {
                    const uint32_t sx0 = dx * 2u;
                    const uint32_t sx1 = sx0 + 1u;
                    const bool have_x1 = sx1 < src_width;

                    float sum = src[sx0 + sy0 * src_width];
                    uint32_t taps = 1u;
                    if (have_x1) {
                        sum += src[sx1 + sy0 * src_width];
                        ++taps;
                    }
                    if (have_y1) {
                        sum += src[sx0 + sy1 * src_width];
                        ++taps;
                        if (have_x1) {
                            sum += src[sx1 + sy1 * src_width];
                            ++taps;
                        }
                    }
                    dst[dx + dy * dst_width] =
                        sum / static_cast<float>(taps);
                }
            }
            return dst;
        }

        // #197: publish the field's GPU residency onto the shared wozzits-rhi
        // registry as an R32F Texture2D resource (the surrogate pattern — the
        // registry owns the GPU texture, the asset is its surrogate). Mirrors
        // publish_resident_gpu_sparse_mesh. Best-effort: a failure is logged and
        // does not fail the compile (the field still resolves on the CPU table,
        // and the legacy render path remains until #195 repoints it).
        //
        // #210: the resident texture is created with its FULL mip chain and each
        // level is uploaded from a CPU box-filter pyramid (mip k = 2^k box-filter
        // of the field). The clipmap VS samples mip == LOD level so coarse rings
        // read a box-filtered height instead of one texel of the full-res field
        // (anti-aliasing the far rings). Level-0 samplers (splat cloud, etc.) read
        // mip 0 and are byte-identical to the single-mip path. Built for every
        // resident scalar field: the residency is published at field-compile time
        // with no consumer information, so there is no clean seam to build the
        // chain only for clipmap-consumed fields; the extra ~33% memory is the
        // accepted cost of the uniform pyramid.
        void publish_resident_scalar_field(
            const wz::asset::AssetKey& key,
            const ScalarFieldData& field,
            wz::rhi::GpuResourceRegistry& gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            // V1: 2D R32F only (depth == 1), matching the texture residency path.
            if (!field.valid()
                || field.depth != 1u
                || field.format != ScalarFieldFormat::Float32)
            {
                return;
            }

            const auto started = std::chrono::steady_clock::now();

            const std::vector<ScalarFieldMipLevel> pyramid =
                build_scalar_field_mip_pyramid(
                    field.values, field.width, field.height);
            const uint32_t mip_levels =
                static_cast<uint32_t>(pyramid.size());

            wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::texture_2d(
                field.width,
                field.height,
                wz::rhi::TextureFormat::R32Float,
                wz::rhi::ResourceUsage_Sampled);
            desc.mip_levels = mip_levels;
            desc.cpu_access = wz::rhi::ResourceCpuAccess::WriteOnce;
            desc.identity = wz::rhi::ResourceIdentity{
                rhi_asset_identity(key, "field_texture"),
                {},
            };

            const wz::rhi::GpuResourceHandle handle = gpu_resources.acquire(desc);
            bool uploaded = handle.valid();

            // Mip 0 is the field itself; each coarser level is a 2x2 box-filter of
            // the level above. Upload every level through the #209 per-mip door.
            for (uint32_t mip = 0u; uploaded && mip < mip_levels; ++mip) {
                const ScalarFieldMipLevel& level = pyramid[mip];
                const uint64_t bytes =
                    static_cast<uint64_t>(level.values.size()) * sizeof(float);
                uploaded =
                    gpu_resources.update_mip(handle, mip, level.values.data(), bytes);
            }

            if (!uploaded) {
                if (handle.valid()) {
                    gpu_resources.release(handle);
                }
                logger.warn("scalar field RHI resident upload failed");
                return;
            }

            if (rhi_resource_tracker) {
                rhi_resource_tracker(key, { desc.identity });
            }

            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset compile: scalar field RHI resident upload "
                + std::to_string(field.width) + "x"
                + std::to_string(field.height)
                + " mips=" + std::to_string(mip_levels)
                + " upload_ms=" + std::to_string(elapsed_ms));
        }

        // Shared tail for every compile path (file-backed / procedural, cache hit
        // / fresh): publish rhi residency when a registry is present, store the
        // field in the CPU table, and return the compiled node.
        wz::asset::AssetNode finalize_scalar_field(
            const wz::asset::AssetNode& input,
            ScalarFieldData data,
            ScalarFieldTable& scalar_field_table,
            wz::rhi::GpuResourceRegistry* gpu_resources,
            const RhiResourceTracker& rhi_resource_tracker,
            wz::Logger& logger)
        {
            if (gpu_resources) {
                publish_resident_scalar_field(
                    input.key,
                    data,
                    *gpu_resources,
                    rhi_resource_tracker,
                    logger);
            }

            const wz::asset::ResourceHandle handle =
                scalar_field_table.add(std::move(data));

            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }
    }


    void register_scalar_field_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_field_table,
        const EngineAssetCacheSettings& cache_settings,
        wz::rhi::GpuResourceRegistry* gpu_resources,
        RhiResourceTracker rhi_resource_tracker)
    {
        // ── Scalar field compiler (file-backed) ───────────────────────────────
        //
        // Dispatches on kScalarFieldFromRawF32Schema.
        // Expects exactly one dependency: a kRawFileSchema node whose compiled
        // payload is a std::vector<uint8_t> of raw float32 bytes.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldFromRawF32Schema,
            .output_type = kAssetTypeScalarField,
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            .parameters = {
                {
                    .name = "width",
                    .type = wz::asset::ParamType::Int,
                    .label = "Width",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
                {
                    .name = "height",
                    .type = wz::asset::ParamType::Int,
                    .label = "Height",
                    .default_num = 1,
                    .min = 1,
                    .max = 65536,
                },
                {
                    .name = "depth",
                    .type = wz::asset::ParamType::Int,
                    .label = "Depth",
                    .default_num = 1,
                    .min = 1,
                    .max = 65536,
                },
                {
                    .name = "domain_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(
                            ScalarFieldDomainKind::Spatial2D),
                    .options = kScalarFieldDomainOptions,
                },
            },
            .compile = [&logger, &scalar_field_table, cache_settings,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate metadata ──────────────────────────────────────

                ScalarFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<ScalarFieldCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc = scalar_field_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }

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

                // Both products are computed in 64-bit (issue #310, A4-C23).
                //
                // As uint32 they WRAPPED, and the wrap was load-bearing rather
                // than theoretical: width=65536, height=16384, depth=1 -- each
                // inside its own declared range -- gives count = 2^30, whose
                // byte count 2^32 wraps to ZERO. So `bytes->size() != 0` was the
                // check, a zero-byte .r32 passed it, and the compiler then
                // allocated 2^30 floats (4 GiB) of zeros and published a
                // 65536x16384 resident texture plus a full CPU mip pyramid.
                // count = 2^30+1 wraps to 4 instead, so a FOUR-byte file bought
                // the same 4 GiB field.
                //
                // ScalarFieldData::count() has the same 32-bit product, so
                // valid() agreed with the wrapped arithmetic instead of
                // catching it; nothing downstream was going to notice.
                //
                // The declared ParamDecl min/max do not bound this -- they are
                // documented as slider range only, and params.get<uint32_t>
                // performs no clamp, so a hand-edited assets.graph.json or an
                // ABI set_param arrives unfiltered.
                const uint64_t count_64 =
                    static_cast<uint64_t>(desc->width)
                    * static_cast<uint64_t>(desc->height)
                    * static_cast<uint64_t>(desc->depth);

                const uint64_t expected_bytes_64 =
                    count_64 * static_cast<uint64_t>(sizeof(float));

                // The field is addressed with 32-bit counts downstream, so a
                // shape that cannot be expressed there is refused here rather
                // than silently truncated into one that can.
                if (count_64 > static_cast<uint64_t>(UINT32_MAX)) {
                    logger.error(
                        "scalar field dimensions overflow a 32-bit sample count: "
                        + std::to_string(desc->width) + "x"
                        + std::to_string(desc->height) + "x"
                        + std::to_string(desc->depth));
                    return compile_failed_node(input);
                }

                const uint32_t count = static_cast<uint32_t>(count_64);
                const uint64_t expected_bytes = expected_bytes_64;

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
                    return finalize_scalar_field(
                        input,
                        std::move(cached_data),
                        scalar_field_table,
                        gpu_resources,
                        rhi_resource_tracker,
                        logger);
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

                // ── 7. Publish residency + store + return compiled node ───────

                return finalize_scalar_field(
                    input,
                    std::move(data),
                    scalar_field_table,
                    gpu_resources,
                    rhi_resource_tracker,
                    logger);
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
            .parameters = {
                {
                    .name = "width",
                    .type = wz::asset::ParamType::Int,
                    .label = "Width",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
                {
                    .name = "height",
                    .type = wz::asset::ParamType::Int,
                    .label = "Height",
                    .default_num = 1,
                    .min = 1,
                    .max = 65536,
                },
                {
                    .name = "depth",
                    .type = wz::asset::ParamType::Int,
                    .label = "Depth",
                    .default_num = 1,
                    .min = 1,
                    .max = 1,
                },
                {
                    .name = "generator",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Generator",
                    .default_num =
                        static_cast<double>(
                            ScalarFieldGenerator::GradientX),
                    .options = kScalarFieldGeneratorOptions,
                },
                {
                    .name = "frequency",
                    .type = wz::asset::ParamType::Float,
                    .label = "Frequency",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1024.0,
                },
                {
                    .name = "amplitude",
                    .type = wz::asset::ParamType::Float,
                    .label = "Amplitude",
                    .default_num = 1.0,
                    .min = -1024.0,
                    .max = 1024.0,
                },
                {
                    .name = "domain_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(
                            ScalarFieldDomainKind::Spatial2D),
                    .options = kScalarFieldDomainOptions,
                },
            },
            .compile = [&logger, &scalar_field_table, cache_settings,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate metadata ──────────────────────────────────────

                ProceduralScalarFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<ProceduralScalarFieldCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            procedural_scalar_field_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }

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
                    return finalize_scalar_field(
                        input,
                        std::move(cached_data),
                        scalar_field_table,
                        gpu_resources,
                        rhi_resource_tracker,
                        logger);
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

                // ── 5. Publish residency + store + return compiled node ───────

                return finalize_scalar_field(
                    input,
                    std::move(data),
                    scalar_field_table,
                    gpu_resources,
                    rhi_resource_tracker,
                    logger);
            }
            });


        // ── Scalar field compiler (procedural terrain) ────────────────────────
        //
        // Dispatches on kScalarFieldTerrainSchema. Fractal noise shaped into a
        // landscape, with a radial basin that flattens the middle so a far layer
        // can ring a near one without erupting through it.
        //
        // Two contracts hold this recipe together, both documented on
        // TerrainScalarFieldCompileDesc:
        //   - output is normalised to exactly [0, 1], matching what a Gaea .r32
        //     arrives with, so the two are interchangeable under one Placement;
        //   - no dial is in world units, so nothing here can drift out of step
        //     with the Placement that supplies the world footprint.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldTerrainSchema,
            .output_type = kAssetTypeScalarField,
            // Every param the desc decoder reads must be declared here, or the
            // editor stores it as a string, pb.get<T> ignores strings, and the
            // dial goes silently dead.
            .parameters = {
                {
                    .name = "name",
                    .type = wz::asset::ParamType::String,
                    .label = "Name",
                },
                {
                    .name = "resolution",
                    .type = wz::asset::ParamType::Int,
                    .label = "Resolution",
                    .default_num = 1024,
                    .min = 2,
                    .max = 8192,
                },
                {
                    .name = "ridge_count",
                    .type = wz::asset::ParamType::Float,
                    .label = "Ridge count",
                    .default_num = 6.0,
                    .min = 1.0,
                    .max = 64.0,
                },
                {
                    .name = "ridginess",
                    .type = wz::asset::ParamType::Float,
                    .label = "Ridginess",
                    .default_num = 0.6,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "roughness",
                    .type = wz::asset::ParamType::Float,
                    .label = "Roughness",
                    .default_num = 0.5,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "detail",
                    .type = wz::asset::ParamType::Int,
                    .label = "Detail",
                    .default_num = 6,
                    .min = 1,
                    .max = 12,
                },
                {
                    .name = "seed",
                    .type = wz::asset::ParamType::Int,
                    .label = "Seed",
                    .default_num = 0,
                    .min = 0,
                    .max = 2147483647.0,
                },
                {
                    .name = "basin_radius",
                    .type = wz::asset::ParamType::Float,
                    .label = "Basin radius",
                    .default_num = 0.35,
                    .min = 0.0,
                    .max = 1.5,
                },
                {
                    .name = "basin_falloff",
                    .type = wz::asset::ParamType::Float,
                    .label = "Basin falloff",
                    .default_num = 0.25,
                    .min = 0.0,
                    .max = 1.5,
                },
                {
                    .name = "basin_depth",
                    .type = wz::asset::ParamType::Float,
                    .label = "Basin depth",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 1.0,
                },
                {
                    .name = "domain_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(
                            ScalarFieldDomainKind::Spatial2D),
                    .options = kScalarFieldDomainOptions,
                },
            },
            .compile = [&logger, &scalar_field_table, cache_settings,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Validate metadata ──────────────────────────────────────

                TerrainScalarFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<TerrainScalarFieldCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc =
                            terrain_scalar_field_desc_from_params(*params);
                        desc = &param_desc;
                    }
                }

                if (!desc) {
                    logger.error("terrain scalar field node missing "
                        "TerrainScalarFieldCompileDesc");
                    return compile_failed_node(input);
                }

                if (desc->resolution < 2) {
                    logger.error(
                        "terrain scalar field needs resolution >= 2 (got "
                        + std::to_string(desc->resolution) + ")");
                    return compile_failed_node(input);
                }

                // A zero here would produce an entirely flat field, which reads
                // as a broken asset rather than as an authored choice — so it
                // fails loudly instead of being clamped behind the author's back.
                if (desc->detail == 0) {
                    logger.error(
                        "terrain scalar field needs detail >= 1 octave");
                    return compile_failed_node(input);
                }

                if (desc->format != ScalarFieldFormat::Float32) {
                    logger.error(
                        "terrain scalar field only supports Float32");
                    return compile_failed_node(input);
                }

                if (!dep_nodes.empty()) {
                    logger.error(
                        "terrain scalar field should not have dependencies");
                    return compile_failed_node(input);
                }

                ScalarFieldData cached_data{};
                if (load_cached_scalar_field_impl(
                        cache_settings, input.key, logger, cached_data))
                {
                    return finalize_scalar_field(
                        input,
                        std::move(cached_data),
                        scalar_field_table,
                        gpu_resources,
                        rhi_resource_tracker,
                        logger);
                }

                // ── 2. Generate values ────────────────────────────────────────

                const uint32_t n = desc->resolution;
                const uint32_t count = n * n;

                // Blend weights are clamped: a lerp weight outside [0, 1] has no
                // meaning. The key factory folds in the AUTHORED value, so two
                // out-of-range dials that clamp together cost a duplicate cache
                // entry but never disagree about what they produced.
                const float ridginess = std::clamp(desc->ridginess, 0.0f, 1.0f);
                const float roughness = std::clamp(desc->roughness, 0.0f, 1.0f);
                const float basin_depth =
                    std::clamp(desc->basin_depth, 0.0f, 1.0f);

                // Field-relative basin geometry: distance from the centre sample
                // divided by the centre-to-edge distance, so 1.0 is the edge
                // midpoint and ~1.414 the corners.
                const float centre = 0.5f * static_cast<float>(n - 1);
                const float inv_half = (centre > 0.0f) ? (1.0f / centre) : 0.0f;
                const float basin_inner = desc->basin_radius;
                const float basin_outer =
                    desc->basin_radius + desc->basin_falloff;

                std::vector<float> values(count);

                for (uint32_t y = 0; y < n; ++y) {
                    for (uint32_t x = 0; x < n; ++x) {
                        // ridge_count cycles span the field exactly, which is
                        // what makes the dial countable on screen.
                        const float u =
                            static_cast<float>(x) / static_cast<float>(n);
                        const float v =
                            static_cast<float>(y) / static_cast<float>(n);

                        const TerrainOctaveSums sums = terrain_octaves(
                            u * desc->ridge_count,
                            v * desc->ridge_count,
                            desc->detail,
                            roughness,
                            desc->seed);

                        float h = sums.plain
                            + ridginess * (sums.ridged - sums.plain);

                        // Multiplicative basin: pulls toward the valley floor
                        // rather than digging a hole, so partial depths keep a
                        // hint of the terrain's character.
                        const float dx = static_cast<float>(x) - centre;
                        const float dy = static_cast<float>(y) - centre;
                        const float r =
                            std::sqrt(dx * dx + dy * dy) * inv_half;

                        float t = 0.0f;
                        if (basin_outer > basin_inner) {
                            const float s = std::clamp(
                                (r - basin_inner) /
                                    (basin_outer - basin_inner),
                                0.0f, 1.0f);
                            t = s * s * (3.0f - 2.0f * s);
                        } else {
                            // Zero falloff is a hard step, not a divide by zero.
                            t = (r >= basin_outer) ? 1.0f : 0.0f;
                        }

                        h *= (1.0f - basin_depth) + basin_depth * t;

                        values[x + y * n] = h;
                    }
                }

                // ── 3. Normalise to exactly [0, 1] ────────────────────────────
                //
                // The swap-compatibility contract: a terrain field and a Gaea
                // .r32 must mean the same thing under one Placement, so peaks
                // land at extent[1] metres either way. Rescaling here rather
                // than trusting the noise to fill the range is what makes
                // "set extent_y = 900 and your peaks are 900 m" literally true.

                float raw_min = values[0];
                float raw_max = values[0];
                for (const float v : values) {
                    if (v < raw_min) raw_min = v;
                    if (v > raw_max) raw_max = v;
                }

                const float span = raw_max - raw_min;
                if (span > 1e-8f) {
                    const float inv_span = 1.0f / span;
                    for (float& v : values) {
                        v = (v - raw_min) * inv_span;
                    }
                } else {
                    // Degenerate but legal: a fully-basined field is flat.
                    for (float& v : values) {
                        v = 0.0f;
                    }
                }

                // ── 4. Validate values; compute min/max ───────────────────────

                float min_val = 0.0f;
                float max_val = 0.0f;

                if (!compute_min_max(values, min_val, max_val,
                                     logger, "terrain scalar field"))
                {
                    return compile_failed_node(input);
                }

                // ── 5. Store in ScalarFieldTable ──────────────────────────────

                ScalarFieldData data;
                data.width = n;
                data.height = n;
                data.depth = 1;
                data.format = desc->format;
                data.domain_kind = desc->domain_kind;
                data.min_value = min_val;
                data.max_value = max_val;
                data.values = std::move(values);

                store_cached_scalar_field(
                    cache_settings, input.key, data, logger);

                // ── 6. Publish residency + store + return compiled node ───────

                return finalize_scalar_field(
                    input,
                    std::move(data),
                    scalar_field_table,
                    gpu_resources,
                    rhi_resource_tracker,
                    logger);
            }
            });


        // ── Scalar field compiler (Gaea .r32 heightmap) ───────────────────────
        //
        // Dispatches on kScalarFieldFromGaeaR32Schema. Same raw float32 bytes as
        // the file-backed recipe, but the dimensions are not authored: they follow
        // Gaea's convention of a square grid whose side is sqrt(sample_count). A
        // file whose sample count is not a perfect square is rejected.

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kScalarFieldFromGaeaR32Schema,
            .output_type = kAssetTypeScalarField,
            .input_ports = {
                { "source_file", kAssetTypeRawFile },
            },
            .parameters = {
                {
                    .name = "domain_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Domain",
                    .default_num =
                        static_cast<double>(
                            ScalarFieldDomainKind::Spatial2D),
                    .options = kScalarFieldDomainOptions,
                },
            },
            .compile = [&logger, &scalar_field_table, cache_settings,
                        gpu_resources, rhi_resource_tracker](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode> dep_nodes,
                std::span<const wz::asset::ResourceHandle>) -> wz::asset::AssetNode
            {
                // ── 1. Resolve domain_kind (typed desc or ParamBlock) ─────────

                GaeaR32ScalarFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<GaeaR32ScalarFieldCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(&input.meta))
                    {
                        param_desc.domain_kind =
                            enum_param(
                                *params,
                                "domain_kind",
                                param_desc.domain_kind,
                                kScalarFieldDomainOptions);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error(
                        "Gaea r32 scalar field node missing "
                        "GaeaR32ScalarFieldCompileDesc");
                    return compile_failed_node(input);
                }

                // ── 2. Disk cache hit ─────────────────────────────────────────

                ScalarFieldData cached_data{};
                if (load_cached_scalar_field_impl(
                        cache_settings, input.key, logger, cached_data))
                {
                    return finalize_scalar_field(
                        input,
                        std::move(cached_data),
                        scalar_field_table,
                        gpu_resources,
                        rhi_resource_tracker,
                        logger);
                }

                // ── 3. Validate dependency ────────────────────────────────────

                if (dep_nodes.empty()) {
                    logger.error(
                        "Gaea r32 scalar field node has no file dependency");
                    return compile_failed_node(input);
                }
                const auto* bytes =
                    std::get_if<std::vector<uint8_t>>(&dep_nodes[0].payload);
                if (!bytes) {
                    logger.error(
                        "Gaea r32 scalar field dep node has no byte payload");
                    return compile_failed_node(input);
                }

                // ── 4. Byte count -> sample count ─────────────────────────────

                if ((bytes->size() % sizeof(float)) != 0u) {
                    logger.error(
                        "Gaea r32 scalar field .r32 size "
                        + std::to_string(bytes->size())
                        + " is not a multiple of 4");
                    return compile_failed_node(input);
                }
                const uint64_t sample_count = bytes->size() / sizeof(float);
                if (sample_count == 0u) {
                    logger.error("Gaea r32 scalar field .r32 is empty");
                    return compile_failed_node(input);
                }

                // ── 5. Derive square dimensions (Gaea convention) ─────────────

                const double side_d =
                    std::sqrt(static_cast<double>(sample_count));
                const uint32_t side = static_cast<uint32_t>(side_d + 0.5);
                if (static_cast<uint64_t>(side) * static_cast<uint64_t>(side)
                    != sample_count)
                {
                    logger.error(
                        "Gaea r32 scalar field sample count "
                        + std::to_string(sample_count)
                        + " is not a perfect square; use the raw-F32 scalar "
                          "field schema with explicit width and height");
                    return compile_failed_node(input);
                }

                // ── 6. Reinterpret bytes as float32 values ────────────────────

                std::vector<float> values(static_cast<size_t>(sample_count));
                std::memcpy(
                    values.data(),
                    bytes->data(),
                    static_cast<size_t>(sample_count) * sizeof(float));

                // ── 7. Validate values; compute min/max ───────────────────────

                float min_val = 0.0f;
                float max_val = 0.0f;
                if (!compute_min_max(values, min_val, max_val,
                                     logger, "Gaea r32 scalar field"))
                {
                    return compile_failed_node(input);
                }

                // ── 8. Build, cache, publish residency, return ────────────────

                ScalarFieldData data;
                data.width = side;
                data.height = side;
                data.depth = 1;
                data.format = ScalarFieldFormat::Float32;
                data.domain_kind = desc->domain_kind;
                data.min_value = min_val;
                data.max_value = max_val;
                data.values = std::move(values);

                store_cached_scalar_field(
                    cache_settings, input.key, data, logger);

                return finalize_scalar_field(
                    input,
                    std::move(data),
                    scalar_field_table,
                    gpu_resources,
                    rhi_resource_tracker,
                    logger);
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

    std::vector<ScalarFieldMipLevel> build_scalar_field_mip_pyramid(
        const std::vector<float>& mip0,
        uint32_t width,
        uint32_t height)
    {
        std::vector<ScalarFieldMipLevel> pyramid;
        if (width == 0u || height == 0u
            || mip0.size()
                != static_cast<size_t>(width) * height)
        {
            return pyramid;
        }

        // Level 0 is the field itself.
        pyramid.push_back(ScalarFieldMipLevel{ width, height, mip0 });

        // Each coarser level is the 2x2 box-filter of the level above, until a
        // 1x1 level is produced (floor(log2(max(w,h)))+1 levels total).
        uint32_t w = width;
        uint32_t h = height;
        while (w > 1u || h > 1u) {
            uint32_t dw = 0u;
            uint32_t dh = 0u;
            std::vector<float> down =
                box_filter_down(pyramid.back().values, w, h, dw, dh);
            pyramid.push_back(
                ScalarFieldMipLevel{ dw, dh, std::move(down) });
            w = dw;
            h = dh;
        }
        return pyramid;
    }

} // namespace wz::engine::assets::internal
