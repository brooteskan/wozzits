// src/engine/assets/collision/collision_compilers.cpp

#include <engine/assets/collision/collision_compilers.h>
#include <engine/assets/disk_cache_checksum.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/engine_asset_library_internal.h>
#include <engine/assets/compiler_version_tokens.h>
#include <engine/assets/disk_cache_keys.h>
#include <engine/assets/disk_cache_paths.h>
#include <engine/assets/placement/placement.h>
#include <engine/assets/scalar_field/scalar_field_compilers.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>
#include <array>
#include <any>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <vector>

namespace wz::engine::assets::internal
{
    namespace
    {
        constexpr uint32_t kCollisionTerrainDiskCacheMagic = 0x43435a57u;
        constexpr uint32_t kCollisionTerrainDiskCacheVersion = 5u;  // #75 B1-C4: +checksum

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

        wz::fs::Path collision_terrain_cache_directory(
            const EngineAssetCacheSettings& cache)
        {
            return disk_cache_asset_directory(
                cache,
                kCollisionTerrainDiskCacheKey.subdirectory);
        }

        wz::fs::Path collision_terrain_cache_path(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key)
        {
            return disk_cache_asset_path(
                cache,
                kCollisionTerrainDiskCacheKey.subdirectory,
                key,
                kCollisionTerrainDiskCacheKey.seed_lo,
                kCollisionTerrainDiskCacheKey.seed_hi);
        }

        void append_float_array(
            std::vector<uint8_t>& out,
            const float* values,
            uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i) {
                append_scalar(out, values[i]);
            }
        }

        bool read_float_array(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            float* values,
            uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i) {
                if (!read_scalar(bytes, offset, values[i])) {
                    return false;
                }
            }
            return true;
        }

        void append_occupancy(
            std::vector<uint8_t>& out,
            const CollisionOccupancyData& occupancy)
        {
            append_scalar(out, static_cast<uint8_t>(occupancy.kind));
            append_scalar(out, static_cast<uint8_t>(occupancy.blocks_movement));
            append_scalar(out, static_cast<uint8_t>(occupancy.queryable));
        }

        bool read_occupancy(
            const std::vector<uint8_t>& bytes,
            size_t& offset,
            CollisionOccupancyData& occupancy)
        {
            uint8_t kind = 0;
            uint8_t blocks = 0;
            uint8_t queryable = 0;
            if (!read_scalar(bytes, offset, kind)
                || !read_scalar(bytes, offset, blocks)
                || !read_scalar(bytes, offset, queryable))
            {
                return false;
            }
            if (!valid_collision_occupancy_kind(kind)) {
                return false;
            }
            occupancy.kind = static_cast<CollisionOccupancyKind>(kind);
            occupancy.blocks_movement = blocks != 0u;
            occupancy.queryable = queryable != 0u;
            return true;
        }

        template<typename T>
        void append_vector_count(std::vector<uint8_t>& out, const std::vector<T>& v)
        {
            append_scalar(out, static_cast<uint64_t>(v.size()));
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
            if (min_bytes_per_entry == 0u) {
                return true;
            }
            const uint64_t remaining =
                static_cast<uint64_t>(bytes.size() - offset);
            return count <= remaining / min_bytes_per_entry;
        }

        std::vector<uint8_t> serialize_collision_asset(
            const wz::asset::AssetKey& key,
            const CollisionAssetData& data)
        {
            std::vector<uint8_t> out;
            out.reserve(
                256u
                + data.points.size() * sizeof(CollisionPoint)
                + data.indices.size() * sizeof(uint32_t)
                + data.triangle_bounds.size() * sizeof(CollisionTriangleBounds)
                + data.height_samples.size() * sizeof(float)
                + data.surface_grid.cell_offsets.size() * sizeof(uint32_t)
                + data.surface_grid.cell_triangle_indices.size() * sizeof(uint32_t)
                + data.surface_grid.cell_bounds.size() * sizeof(CollisionTriangleBounds));

            append_scalar(out, kCollisionTerrainDiskCacheMagic);
            append_scalar(out, kCollisionTerrainDiskCacheVersion);
            append_scalar(out, kCollisionCompilerVersion);
            append_asset_key(out, key);
            append_scalar(out, static_cast<uint8_t>(data.source_kind));
            append_scalar(out, static_cast<uint8_t>(data.shape_kind));
            append_occupancy(out, data.occupancy);
            append_asset_key(out, data.source_asset);
            append_asset_key(out, data.geometry_asset);
            append_float_array(out, data.bounds_min, 3);
            append_float_array(out, data.bounds_max, 3);

            append_vector_count(out, data.points);
            append_raw_bytes(
                out,
                data.points.data(),
                data.points.size() * sizeof(CollisionPoint));

            append_vector_count(out, data.indices);
            append_raw_bytes(
                out,
                data.indices.data(),
                data.indices.size() * sizeof(uint32_t));

            append_vector_count(out, data.triangle_bounds);
            append_raw_bytes(
                out,
                data.triangle_bounds.data(),
                data.triangle_bounds.size()
                    * sizeof(CollisionTriangleBounds));

            append_scalar(out, data.surface_grid.origin_x);
            append_scalar(out, data.surface_grid.origin_z);
            append_scalar(out, data.surface_grid.cell_size_x);
            append_scalar(out, data.surface_grid.cell_size_z);
            append_scalar(out, data.surface_grid.cells_x);
            append_scalar(out, data.surface_grid.cells_z);

            append_vector_count(out, data.surface_grid.cell_offsets);
            append_raw_bytes(
                out,
                data.surface_grid.cell_offsets.data(),
                data.surface_grid.cell_offsets.size() * sizeof(uint32_t));
            append_vector_count(out, data.surface_grid.cell_triangle_indices);
            append_raw_bytes(
                out,
                data.surface_grid.cell_triangle_indices.data(),
                data.surface_grid.cell_triangle_indices.size()
                    * sizeof(uint32_t));
            append_vector_count(out, data.surface_grid.cell_bounds);
            append_raw_bytes(
                out,
                data.surface_grid.cell_bounds.data(),
                data.surface_grid.cell_bounds.size()
                    * sizeof(CollisionTriangleBounds));

            append_asset_key(out, data.height_field);
            append_asset_key(out, data.mesh);
            append_float_array(out, data.origin, 2);
            append_float_array(out, data.size, 2);
            append_scalar(out, data.resolution_x);
            append_scalar(out, data.resolution_y);
            append_scalar(out, data.vertical_scale);
            append_scalar(out, data.base_height);
            append_scalar(out, data.min_height);
            append_scalar(out, data.max_height);

            append_vector_count(out, data.height_samples);
            append_raw_bytes(
                out,
                data.height_samples.data(),
                data.height_samples.size() * sizeof(float));

            append_scalar(out, data.source_triangle_count);
            append_scalar(out, data.accepted_triangle_count);
            append_scalar(out, static_cast<uint8_t>(data.supports_bounds_query));
            append_scalar(out, static_cast<uint8_t>(data.supports_height_query));
            append_scalar(out, static_cast<uint8_t>(data.supports_ray_query));
            append_scalar(out, static_cast<uint8_t>(data.supports_overlap_query));
            append_scalar(out, static_cast<uint8_t>(data.placement_driven));
            append_scalar(out, data.render_lod_base_resolution);
            append_scalar(out, data.render_lod_level_count);
            append_scalar(out, data.render_lod_cell_size);
            append_scalar(out, static_cast<uint8_t>(
                data.constrain_to_drawn_surface));
            wz::engine::assets::append_disk_cache_checksum(out);
            return out;
        }

        bool deserialize_collision_asset(
            const std::vector<uint8_t>& bytes_in,
            const wz::asset::AssetKey& expected_key,
            CollisionAssetData& data)
        {
            std::vector<uint8_t> bytes;
            if (!wz::engine::assets::verify_and_strip_disk_cache_checksum(
                    bytes_in, bytes)) {
                return false;
            }
            size_t offset = 0;
            uint32_t magic = 0;
            uint32_t version = 0;
            uint64_t compiler_version = 0;
            if (!read_scalar(bytes, offset, magic)
                || !read_scalar(bytes, offset, version)
                || !read_scalar(bytes, offset, compiler_version)
                || magic != kCollisionTerrainDiskCacheMagic
                || version != kCollisionTerrainDiskCacheVersion
                || compiler_version != kCollisionCompilerVersion)
            {
                return false;
            }

            wz::asset::AssetKey stored_key{};
            if (!read_asset_key(bytes, offset, stored_key)
                || stored_key != expected_key)
            {
                return false;
            }

            uint8_t source_kind = 0;
            uint8_t shape_kind = 0;
            if (!read_scalar(bytes, offset, source_kind)
                || !read_scalar(bytes, offset, shape_kind)
                || !read_occupancy(bytes, offset, data.occupancy)
                || !read_asset_key(bytes, offset, data.source_asset)
                || !read_asset_key(bytes, offset, data.geometry_asset)
                || !read_float_array(bytes, offset, data.bounds_min, 3)
                || !read_float_array(bytes, offset, data.bounds_max, 3))
            {
                return false;
            }
            if (!valid_collision_source_kind(source_kind)
                || !valid_collision_shape_kind(shape_kind))
            {
                return false;
            }
            data.source_kind = static_cast<CollisionSourceKind>(source_kind);
            data.shape_kind = static_cast<CollisionShapeKind>(shape_kind);

            uint64_t count = 0;
            if (!read_vector_count(bytes, offset, count, 3u * sizeof(float))) {
                return false;
            }
            data.points.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.points.data(),
                    data.points.size() * sizeof(CollisionPoint)))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(uint32_t))) {
                return false;
            }
            data.indices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.indices.data(),
                    data.indices.size() * sizeof(uint32_t)))
            {
                return false;
            }

            if (!read_vector_count(
                    bytes,
                    offset,
                    count,
                    sizeof(CollisionTriangleBounds)))
            {
                return false;
            }
            data.triangle_bounds.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.triangle_bounds.data(),
                    data.triangle_bounds.size()
                        * sizeof(CollisionTriangleBounds)))
            {
                return false;
            }

            if (!read_scalar(bytes, offset, data.surface_grid.origin_x)
                || !read_scalar(bytes, offset, data.surface_grid.origin_z)
                || !read_scalar(bytes, offset, data.surface_grid.cell_size_x)
                || !read_scalar(bytes, offset, data.surface_grid.cell_size_z)
                || !read_scalar(bytes, offset, data.surface_grid.cells_x)
                || !read_scalar(bytes, offset, data.surface_grid.cells_z))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(uint32_t))) {
                return false;
            }
            data.surface_grid.cell_offsets.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.surface_grid.cell_offsets.data(),
                    data.surface_grid.cell_offsets.size() * sizeof(uint32_t)))
            {
                return false;
            }
            if (!read_vector_count(bytes, offset, count, sizeof(uint32_t))) {
                return false;
            }
            data.surface_grid.cell_triangle_indices.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.surface_grid.cell_triangle_indices.data(),
                    data.surface_grid.cell_triangle_indices.size()
                        * sizeof(uint32_t)))
            {
                return false;
            }
            if (!read_vector_count(
                    bytes,
                    offset,
                    count,
                    sizeof(CollisionTriangleBounds)))
            {
                return false;
            }
            data.surface_grid.cell_bounds.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.surface_grid.cell_bounds.data(),
                    data.surface_grid.cell_bounds.size()
                        * sizeof(CollisionTriangleBounds)))
            {
                return false;
            }

            if (!read_asset_key(bytes, offset, data.height_field)
                || !read_asset_key(bytes, offset, data.mesh)
                || !read_float_array(bytes, offset, data.origin, 2)
                || !read_float_array(bytes, offset, data.size, 2)
                || !read_scalar(bytes, offset, data.resolution_x)
                || !read_scalar(bytes, offset, data.resolution_y)
                || !read_scalar(bytes, offset, data.vertical_scale)
                || !read_scalar(bytes, offset, data.base_height)
                || !read_scalar(bytes, offset, data.min_height)
                || !read_scalar(bytes, offset, data.max_height))
            {
                return false;
            }

            if (!read_vector_count(bytes, offset, count, sizeof(float))) {
                return false;
            }
            data.height_samples.resize(static_cast<size_t>(count));
            if (!read_raw_bytes(
                    bytes,
                    offset,
                    data.height_samples.data(),
                    data.height_samples.size() * sizeof(float)))
            {
                return false;
            }

            uint8_t supports_bounds = 0;
            uint8_t supports_height = 0;
            uint8_t supports_ray = 0;
            uint8_t supports_overlap = 0;
            uint8_t placement_driven = 0;
            if (!read_scalar(bytes, offset, data.source_triangle_count)
                || !read_scalar(bytes, offset, data.accepted_triangle_count)
                || !read_scalar(bytes, offset, supports_bounds)
                || !read_scalar(bytes, offset, supports_height)
                || !read_scalar(bytes, offset, supports_ray)
                || !read_scalar(bytes, offset, supports_overlap)
                || !read_scalar(bytes, offset, placement_driven))
            {
                return false;
            }
            data.supports_bounds_query = supports_bounds != 0u;
            data.supports_height_query = supports_height != 0u;
            data.supports_ray_query = supports_ray != 0u;
            data.supports_overlap_query = supports_overlap != 0u;
            data.placement_driven = placement_driven != 0u;
            uint8_t drawn_surface = 0u;
            if (!read_scalar(bytes, offset, data.render_lod_base_resolution)
                || !read_scalar(bytes, offset, data.render_lod_level_count)
                || !read_scalar(bytes, offset, data.render_lod_cell_size)
                || !read_scalar(bytes, offset, drawn_surface))
            {
                return false;
            }
            data.constrain_to_drawn_surface = drawn_surface != 0u;
            return offset == bytes.size();
        }

        // Populate CollisionAssetData::height_mips from height_samples, or clear
        // them when this asset has no render-LOD reconstruction to serve.
        //
        // The gate matches make_height_field_ray_lod's own enable condition, so
        // the pyramid exists exactly when something will read it: a heightfield
        // nobody draws as a clipmap keeps its 0-byte vector. Level 0 is dropped
        // because it is height_samples verbatim -- storing the full chain would
        // double the resident cost of the largest member for no gain.
        //
        // Called from the producers AND after a disk-cache load, since the mips
        // are derived rather than serialized.
        void build_collision_height_mips(CollisionAssetData& data)
        {
            data.height_mips.clear();
            if (data.shape_kind != CollisionShapeKind::TerrainHeightField
                || data.render_lod_level_count < 1u
                || data.render_lod_base_resolution < 2u
                || data.resolution_x < 2u
                || data.resolution_y < 2u
                || data.height_samples.size()
                    != static_cast<size_t>(data.resolution_x)
                        * data.resolution_y)
            {
                return;
            }

            // The SAME builder the resident GPU texture's pyramid comes from,
            // so the CPU levels are the GPU levels rather than a lookalike.
            std::vector<ScalarFieldMipLevel> pyramid =
                build_scalar_field_mip_pyramid(
                    data.height_samples,
                    data.resolution_x,
                    data.resolution_y);
            if (pyramid.size() < 2u) {
                return;   // a 1x1 field has no coarser level
            }

            data.height_mips.reserve(pyramid.size() - 1u);
            for (size_t level = 1u; level < pyramid.size(); ++level) {
                data.height_mips.push_back(CollisionHeightMipLevel{
                    .width = pyramid[level].width,
                    .height = pyramid[level].height,
                    .values = std::move(pyramid[level].values),
                });
            }
        }

        bool load_cached_terrain_collision_impl(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            wz::Logger& logger,
            CollisionAssetData& data)
        {
            if (!cache.enabled || cache.root.empty()) {
                return false;
            }

            const wz::fs::Path path = collision_terrain_cache_path(cache, key);
            const auto started = std::chrono::steady_clock::now();
            const auto bytes = wz::fs::read_file(path);
            if (!bytes) {
                logger.info("asset disk cache miss: terrain collision " + path);
                return false;
            }

            CollisionAssetData loaded{};
            if (!deserialize_collision_asset(bytes.value, key, loaded)
                || !loaded.valid())
            {
                logger.warn(
                    "asset disk cache ignored invalid terrain collision: "
                    + path);
                return false;
            }

            data = std::move(loaded);
            // Derived, not serialized: rebuild from the samples we just read so
            // a cache hit and a fresh compile produce identical assets.
            build_collision_height_mips(data);
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
            logger.info(
                "asset disk cache hit: terrain collision "
                + path
                + " ms="
                + std::to_string(elapsed));
            return true;
        }

        void store_cached_terrain_collision(
            const EngineAssetCacheSettings& cache,
            const wz::asset::AssetKey& key,
            const CollisionAssetData& data,
            wz::Logger& logger)
        {
            if (!cache.enabled || cache.root.empty() || !data.valid()) {
                return;
            }

            const wz::fs::Path directory =
                collision_terrain_cache_directory(cache);
            if (wz::fs::create_directories(directory)
                != wz::fs::FileError::None)
            {
                logger.warn(
                    "asset disk cache directory unavailable: " + directory);
                return;
            }

            const wz::fs::Path path = collision_terrain_cache_path(cache, key);
            const std::vector<uint8_t> bytes =
                serialize_collision_asset(key, data);
            const wz::fs::FileError err =
                wz::fs::write_file(path, bytes, true);
            if (err != wz::fs::FileError::None)
            {
                logger.warn(
                    "asset disk cache write failed: terrain collision "
                    + path
                    + " error="
                    + std::to_string(static_cast<int>(err)));
                return;
            }

            logger.info(
                "asset disk cache stored: terrain collision "
                + path
                + " bytes="
                + std::to_string(bytes.size()));
        }

        wz::asset::AssetNode compiled_collision_node(
            const wz::asset::AssetNode& input,
            wz::asset::ResourceHandle handle)
        {
            wz::asset::AssetNode out = input;
            out.stage = wz::asset::AssetStage::Compiled;
            out.payload = handle;
            return out;
        }

        constexpr std::array<std::string_view, 2>
            kMeshCollisionBuildMethodOptions = {
                "Bounds",
                "Triangle mesh",
            };

        constexpr std::array<CollisionBuildMethod, 2>
            kMeshCollisionBuildMethodValues = {
                CollisionBuildMethod::Bounds,
                CollisionBuildMethod::TriangleMesh,
            };

        constexpr std::array<std::string_view, 3>
            kTerrainCollisionBuildMethodOptions = {
                "Bounds",
                "Terrain surface",
                "Projection height field",
            };

        constexpr std::array<CollisionBuildMethod, 3>
            kTerrainCollisionBuildMethodValues = {
                CollisionBuildMethod::Bounds,
                CollisionBuildMethod::TerrainSurface,
                CollisionBuildMethod::TerrainProjectionHeightField,
            };

        constexpr std::array<std::string_view, 4>
            kCollisionOccupancyKindOptions = {
                "Solid",
                "Surface",
                "Walkable surface",
                "Sensor",
            };

        template<std::size_t Count>
        CollisionBuildMethod build_method_param(
            const wz::asset::ParamBlock& params,
            std::string_view name,
            CollisionBuildMethod fallback,
            const std::array<CollisionBuildMethod, Count>& values)
        {
            const int64_t value = params.get<int64_t>(name, -1);
            if (value >= 0 && value < static_cast<int64_t>(Count)) {
                return values[static_cast<size_t>(value)];
            }
            return fallback;
        }

        CollisionOccupancyData occupancy_param(
            const wz::asset::ParamBlock& params,
            const CollisionOccupancyData& fallback)
        {
            CollisionOccupancyData occupancy = fallback;
            const int64_t kind =
                params.get<int64_t>(
                    "occupancy_kind",
                    static_cast<int64_t>(occupancy.kind));
            if (kind >= 0
                && kind
                    < static_cast<int64_t>(
                        kCollisionOccupancyKindOptions.size()))
            {
                occupancy.kind = static_cast<CollisionOccupancyKind>(kind);
            }
            occupancy.blocks_movement =
                params.get<bool>(
                    "occupancy_blocks_movement",
                    occupancy.blocks_movement);
            occupancy.queryable =
                params.get<bool>("occupancy_queryable", occupancy.queryable);
            return occupancy;
        }

        CollisionFromMeshCompileDesc collision_from_mesh_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode* const> dep_nodes)
        {
            CollisionFromMeshCompileDesc desc{};
            if (!dep_nodes.empty()) {
                desc.mesh = dep_nodes[0]->key;
            }
            desc.build_method =
                build_method_param(
                    params,
                    "build_method",
                    desc.build_method,
                    kMeshCollisionBuildMethodValues);
            desc.occupancy = occupancy_param(params, desc.occupancy);
            return desc;
        }

        CollisionFromTerrainCompileDesc
        collision_from_terrain_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode* const> dep_nodes)
        {
            CollisionFromTerrainCompileDesc desc{};
            if (!dep_nodes.empty()) {
                desc.terrain = dep_nodes[0]->key;
            }
            desc.build_method =
                build_method_param(
                    params,
                    "build_method",
                    desc.build_method,
                    kTerrainCollisionBuildMethodValues);
            desc.occupancy = occupancy_param(params, desc.occupancy);
            // Cap the authored resolution: the declared max was 65536, a ~17 GB
            // resample allocation. 4096 is a fine collision resolution and bounds
            // the resample at ~64 MiB. (C1-S4, #314)
            desc.projection_resolution_x = (std::min)(
                params.get<uint32_t>(
                    "projection_resolution_x",
                    desc.projection_resolution_x),
                4096u);
            desc.projection_resolution_y = (std::min)(
                params.get<uint32_t>(
                    "projection_resolution_y",
                    desc.projection_resolution_y),
                4096u);
            return desc;
        }

        CollisionFromHeightFieldCompileDesc
        collision_from_height_field_desc_from_params(
            const wz::asset::ParamBlock& params,
            std::span<const wz::asset::AssetNode* const> dep_nodes)
        {
            CollisionFromHeightFieldCompileDesc desc{};
            // Locate the scalar field by TYPE, not positional index: the recipe
            // now has an OPTIONAL placement port (issue #218), so dep ordering is
            // not guaranteed and dep_nodes[0] could be the placement. This key is
            // recorded as the collision's source_asset/geometry_asset identity,
            // so it must be the scalar field. The common case (field at dep[0])
            // is unchanged.
            for (const wz::asset::AssetNode* dep : dep_nodes) {
                if (dep->type == kAssetTypeScalarField) {
                    desc.height_field = dep->key;
                    break;
                }
            }
            desc.origin[0] =
                static_cast<float>(params.get<double>("origin_x", desc.origin[0]));
            desc.origin[1] =
                static_cast<float>(params.get<double>("origin_z", desc.origin[1]));
            desc.size[0] =
                static_cast<float>(params.get<double>("size_x", desc.size[0]));
            desc.size[1] =
                static_cast<float>(params.get<double>("size_z", desc.size[1]));
            desc.vertical_scale =
                static_cast<float>(
                    params.get<double>("vertical_scale", desc.vertical_scale));
            desc.base_height =
                static_cast<float>(
                    params.get<double>("base_height", desc.base_height));
            desc.occupancy = occupancy_param(params, desc.occupancy);
            // Cap the authored resolution (C1-S4, #314; see the sibling above).
            desc.projection_resolution_x = (std::min)(
                params.get<uint32_t>(
                    "projection_resolution_x",
                    desc.projection_resolution_x),
                4096u);
            desc.projection_resolution_y = (std::min)(
                params.get<uint32_t>(
                    "projection_resolution_y",
                    desc.projection_resolution_y),
                4096u);
            desc.render_lod_base_resolution =
                params.get<uint32_t>(
                    "render_lod_base_resolution",
                    desc.render_lod_base_resolution);
            desc.render_lod_level_count =
                params.get<uint32_t>(
                    "render_lod_level_count",
                    desc.render_lod_level_count);
            desc.constrain_to_drawn_surface =
                params.get<bool>(
                    "constrain_to_drawn_surface",
                    desc.constrain_to_drawn_surface);
            return desc;
        }

        void copy_mesh_bounds(
            float dst_min[3],
            float dst_max[3],
            const MeshData& mesh)
        {
            for (int axis = 0; axis < 3; ++axis) {
                dst_min[axis] = mesh.vertices[0].position[axis];
                dst_max[axis] = mesh.vertices[0].position[axis];
            }

            for (const auto& vertex : mesh.vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    dst_min[axis] =
                        std::min(dst_min[axis], vertex.position[axis]);
                    dst_max[axis] =
                        std::max(dst_max[axis], vertex.position[axis]);
                }
            }
        }

        void copy_bounds(
            float dst_min[3],
            float dst_max[3],
            const float src_min[3],
            const float src_max[3])
        {
            for (int axis = 0; axis < 3; ++axis) {
                dst_min[axis] = src_min[axis];
                dst_max[axis] = src_max[axis];
            }
        }

        CollisionAssetData collision_from_mesh(
            const CollisionFromMeshCompileDesc& desc,
            const MeshData& mesh)
        {
            CollisionAssetData data{};
            data.source_kind = CollisionSourceKind::Mesh;
            data.source_asset = desc.mesh;
            data.geometry_asset = desc.mesh;
            data.mesh = desc.mesh;
            data.occupancy = desc.occupancy;
            data.source_triangle_count =
                static_cast<uint32_t>(mesh.indices.size() / 3u);
            data.accepted_triangle_count = data.source_triangle_count;
            copy_mesh_bounds(data.bounds_min, data.bounds_max, mesh);

            if (desc.build_method == CollisionBuildMethod::Bounds) {
                data.shape_kind = CollisionShapeKind::Bounds;
                data.supports_overlap_query = true;
                return data;
            }

            data.shape_kind = CollisionShapeKind::TriangleMesh;
            data.points.reserve(mesh.vertices.size());
            for (const auto& vertex : mesh.vertices) {
                CollisionPoint point{};
                point.position[0] = vertex.position[0];
                point.position[1] = vertex.position[1];
                point.position[2] = vertex.position[2];
                data.points.push_back(point);
            }
            data.indices = mesh.indices;
            data.supports_ray_query = true;
            data.supports_overlap_query =
                desc.occupancy.kind == CollisionOccupancyKind::Solid;
            return data;
        }

        void build_triangle_bounds_and_grid(CollisionAssetData& data)
        {
            const uint32_t triangle_count =
                static_cast<uint32_t>(data.indices.size() / 3u);
            data.triangle_bounds.clear();
            data.triangle_bounds.reserve(triangle_count);

            std::vector<std::vector<uint32_t>> cell_triangles;
            const float span_x = data.bounds_max[0] - data.bounds_min[0];
            const float span_z = data.bounds_max[2] - data.bounds_min[2];
            if (triangle_count > 0 && span_x > 0.0f && span_z > 0.0f) {
                const uint32_t cells =
                    (std::min)(
                        128u,
                        (std::max)(
                            1u,
                            static_cast<uint32_t>(
                                std::sqrt(
                                    static_cast<float>(triangle_count) / 8.0f))));
                data.surface_grid.origin_x = data.bounds_min[0];
                data.surface_grid.origin_z = data.bounds_min[2];
                data.surface_grid.cells_x = cells;
                data.surface_grid.cells_z = cells;
                data.surface_grid.cell_size_x = span_x / static_cast<float>(cells);
                data.surface_grid.cell_size_z = span_z / static_cast<float>(cells);
                cell_triangles.resize(static_cast<size_t>(cells) * cells);
                data.surface_grid.cell_bounds.resize(cell_triangles.size());
                for (auto& bounds : data.surface_grid.cell_bounds) {
                    bounds.min[0] = FLT_MAX;
                    bounds.min[1] = FLT_MAX;
                    bounds.min[2] = FLT_MAX;
                    bounds.max[0] = -FLT_MAX;
                    bounds.max[1] = -FLT_MAX;
                    bounds.max[2] = -FLT_MAX;
                }
            }

            for (uint32_t tri = 0; tri < triangle_count; ++tri) {
                const uint32_t ia = data.indices[tri * 3u + 0u];
                const uint32_t ib = data.indices[tri * 3u + 1u];
                const uint32_t ic = data.indices[tri * 3u + 2u];
                if (ia >= data.points.size()
                    || ib >= data.points.size()
                    || ic >= data.points.size())
                {
                    // Preserve index alignment but push an INVERTED (never-
                    // overlap) bound: the all-zero default reads as a real
                    // degenerate collider at the local origin, which the no-grid
                    // fallback overlap test then hits for any actor straddling
                    // (0,0,0). (C1-S9, #314)
                    CollisionTriangleBounds degenerate{};
                    degenerate.min[0] = FLT_MAX;
                    degenerate.min[1] = FLT_MAX;
                    degenerate.min[2] = FLT_MAX;
                    degenerate.max[0] = -FLT_MAX;
                    degenerate.max[1] = -FLT_MAX;
                    degenerate.max[2] = -FLT_MAX;
                    data.triangle_bounds.push_back(degenerate);
                    continue;
                }

                const CollisionPoint& a = data.points[ia];
                const CollisionPoint& b = data.points[ib];
                const CollisionPoint& c = data.points[ic];
                CollisionTriangleBounds bounds{};
                for (int axis = 0; axis < 3; ++axis) {
                    bounds.min[axis] =
                        (std::min)({
                            a.position[axis],
                            b.position[axis],
                            c.position[axis],
                        });
                    bounds.max[axis] =
                        (std::max)({
                            a.position[axis],
                            b.position[axis],
                            c.position[axis],
                        });
                }
                data.triangle_bounds.push_back(bounds);

                if (cell_triangles.empty()) {
                    continue;
                }

                auto cell_index = [](float value, float origin, float size, uint32_t count) {
                    const float normalized = (value - origin) / size;
                    const int raw = static_cast<int>(std::floor(normalized));
                    return static_cast<uint32_t>(
                        (std::clamp)(raw, 0, static_cast<int>(count) - 1));
                };

                const uint32_t min_x = cell_index(
                    bounds.min[0],
                    data.surface_grid.origin_x,
                    data.surface_grid.cell_size_x,
                    data.surface_grid.cells_x);
                const uint32_t max_x = cell_index(
                    bounds.max[0],
                    data.surface_grid.origin_x,
                    data.surface_grid.cell_size_x,
                    data.surface_grid.cells_x);
                const uint32_t min_z = cell_index(
                    bounds.min[2],
                    data.surface_grid.origin_z,
                    data.surface_grid.cell_size_z,
                    data.surface_grid.cells_z);
                const uint32_t max_z = cell_index(
                    bounds.max[2],
                    data.surface_grid.origin_z,
                    data.surface_grid.cell_size_z,
                    data.surface_grid.cells_z);

                for (uint32_t z = min_z; z <= max_z; ++z) {
                    for (uint32_t x = min_x; x <= max_x; ++x) {
                        const size_t cell =
                            static_cast<size_t>(z)
                                * data.surface_grid.cells_x
                            + x;
                        cell_triangles[cell].push_back(tri);

                        auto& cell_bounds =
                            data.surface_grid.cell_bounds[cell];
                        for (int axis = 0; axis < 3; ++axis) {
                            cell_bounds.min[axis] =
                                (std::min)(
                                    cell_bounds.min[axis],
                                    bounds.min[axis]);
                            cell_bounds.max[axis] =
                                (std::max)(
                                    cell_bounds.max[axis],
                                    bounds.max[axis]);
                        }
                    }
                }
            }

            if (cell_triangles.empty()) {
                return;
            }

            data.surface_grid.cell_offsets.clear();
            data.surface_grid.cell_offsets.reserve(cell_triangles.size() + 1u);
            data.surface_grid.cell_triangle_indices.clear();
            data.surface_grid.cell_offsets.push_back(0u);
            for (const auto& cell : cell_triangles) {
                data.surface_grid.cell_triangle_indices.insert(
                    data.surface_grid.cell_triangle_indices.end(),
                    cell.begin(),
                    cell.end());
                data.surface_grid.cell_offsets.push_back(
                    static_cast<uint32_t>(
                        data.surface_grid.cell_triangle_indices.size()));
            }
        }

        bool triangle_height_at_xz(
            float x,
            float z,
            const CollisionPoint& a,
            const CollisionPoint& b,
            const CollisionPoint& c,
            float& out_y) noexcept
        {
            const float ax = a.position[0];
            const float ay = a.position[1];
            const float az = a.position[2];
            const float bx = b.position[0];
            const float by = b.position[1];
            const float bz = b.position[2];
            const float cx = c.position[0];
            const float cy = c.position[1];
            const float cz = c.position[2];
            const float denom =
                (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (std::abs(denom) <= 1e-8f) {
                return false;
            }

            const float u =
                ((bz - cz) * (x - cx) + (cx - bx) * (z - cz))
                / denom;
            const float v =
                ((cz - az) * (x - cx) + (ax - cx) * (z - cz))
                / denom;
            const float w = 1.0f - u - v;
            constexpr float kEpsilon = 1e-5f;
            if (u < -kEpsilon || v < -kEpsilon || w < -kEpsilon) {
                return false;
            }

            out_y = u * ay + v * by + w * cy;
            return true;
        }

        void fill_missing_projection_samples(
            std::vector<float>& samples,
            std::vector<uint8_t>& hit,
            uint32_t resolution_x,
            uint32_t resolution_y,
            float fallback_height)
        {
            if (std::all_of(
                    hit.begin(),
                    hit.end(),
                    [](uint8_t value) { return value != 0u; }))
            {
                return;
            }

            for (uint32_t pass = 0;
                pass < resolution_x + resolution_y;
                ++pass)
            {
                bool changed = false;
                std::vector<float> next_samples = samples;
                std::vector<uint8_t> next_hit = hit;

                for (uint32_t z = 0; z < resolution_y; ++z) {
                    for (uint32_t x = 0; x < resolution_x; ++x) {
                        const size_t index =
                            static_cast<size_t>(z) * resolution_x + x;
                        if (hit[index]) {
                            continue;
                        }

                        float sum = 0.0f;
                        uint32_t count = 0;
                        const int offsets[][2] = {
                            { -1, 0 },
                            { 1, 0 },
                            { 0, -1 },
                            { 0, 1 },
                        };
                        for (const auto& offset : offsets) {
                            const int nx = static_cast<int>(x) + offset[0];
                            const int nz = static_cast<int>(z) + offset[1];
                            if (nx < 0
                                || nz < 0
                                || nx >= static_cast<int>(resolution_x)
                                || nz >= static_cast<int>(resolution_y))
                            {
                                continue;
                            }
                            const size_t neighbor =
                                static_cast<size_t>(nz) * resolution_x
                                + static_cast<size_t>(nx);
                            if (!hit[neighbor]) {
                                continue;
                            }
                            sum += samples[neighbor];
                            ++count;
                        }

                        if (count > 0u) {
                            next_samples[index] =
                                sum / static_cast<float>(count);
                            next_hit[index] = 1u;
                            changed = true;
                        }
                    }
                }

                samples = std::move(next_samples);
                hit = std::move(next_hit);
                if (!changed) {
                    break;
                }
            }

            for (size_t i = 0; i < samples.size(); ++i) {
                if (!hit[i]) {
                    samples[i] = fallback_height;
                    hit[i] = 1u;
                }
            }
        }

        bool project_mesh_terrain_to_heightfield(
            CollisionAssetData& data,
            const TerrainAssetData& terrain,
            uint32_t resolution_x,
            uint32_t resolution_y)
        {
            if (resolution_x < 2u
                || resolution_y < 2u
                || terrain.size[0] <= 0.0f
                || terrain.size[1] <= 0.0f)
            {
                return false;
            }

            const size_t sample_count =
                static_cast<size_t>(resolution_x) * resolution_y;
            std::vector<float> samples(
                sample_count,
                -std::numeric_limits<float>::infinity());
            std::vector<uint8_t> hit(sample_count, 0u);
            const float step_x =
                terrain.size[0] / static_cast<float>(resolution_x - 1u);
            const float step_z =
                terrain.size[1] / static_cast<float>(resolution_y - 1u);

            auto sample_index = [](float value, float origin, float step) {
                return static_cast<int>(
                    std::floor((value - origin) / step));
            };

            for (size_t tri = 0;
                tri + 2 < terrain.mesh_surface_indices.size();
                tri += 3)
            {
                const uint32_t ia = terrain.mesh_surface_indices[tri + 0u];
                const uint32_t ib = terrain.mesh_surface_indices[tri + 1u];
                const uint32_t ic = terrain.mesh_surface_indices[tri + 2u];
                if (ia * 3u + 2u >= terrain.mesh_surface_points.size()
                    || ib * 3u + 2u >= terrain.mesh_surface_points.size()
                    || ic * 3u + 2u >= terrain.mesh_surface_points.size())
                {
                    continue;
                }

                const CollisionPoint a{
                    .position = {
                        terrain.mesh_surface_points[ia * 3u + 0u],
                        terrain.mesh_surface_points[ia * 3u + 1u],
                        terrain.mesh_surface_points[ia * 3u + 2u],
                    },
                };
                const CollisionPoint b{
                    .position = {
                        terrain.mesh_surface_points[ib * 3u + 0u],
                        terrain.mesh_surface_points[ib * 3u + 1u],
                        terrain.mesh_surface_points[ib * 3u + 2u],
                    },
                };
                const CollisionPoint c{
                    .position = {
                        terrain.mesh_surface_points[ic * 3u + 0u],
                        terrain.mesh_surface_points[ic * 3u + 1u],
                        terrain.mesh_surface_points[ic * 3u + 2u],
                    },
                };

                const float min_x =
                    (std::min)({ a.position[0], b.position[0], c.position[0] });
                const float max_x =
                    (std::max)({ a.position[0], b.position[0], c.position[0] });
                const float min_z =
                    (std::min)({ a.position[2], b.position[2], c.position[2] });
                const float max_z =
                    (std::max)({ a.position[2], b.position[2], c.position[2] });
                const int min_ix = (std::clamp)(
                    sample_index(min_x, terrain.origin[0], step_x),
                    0,
                    static_cast<int>(resolution_x) - 1);
                const int max_ix = (std::clamp)(
                    sample_index(max_x, terrain.origin[0], step_x) + 1,
                    0,
                    static_cast<int>(resolution_x) - 1);
                const int min_iz = (std::clamp)(
                    sample_index(min_z, terrain.origin[1], step_z),
                    0,
                    static_cast<int>(resolution_y) - 1);
                const int max_iz = (std::clamp)(
                    sample_index(max_z, terrain.origin[1], step_z) + 1,
                    0,
                    static_cast<int>(resolution_y) - 1);

                for (int z = min_iz; z <= max_iz; ++z) {
                    const float world_z =
                        terrain.origin[1] + step_z * static_cast<float>(z);
                    for (int x = min_ix; x <= max_ix; ++x) {
                        const float world_x =
                            terrain.origin[0]
                            + step_x * static_cast<float>(x);
                        float height = 0.0f;
                        if (!triangle_height_at_xz(
                                world_x,
                                world_z,
                                a,
                                b,
                                c,
                                height))
                        {
                            continue;
                        }
                        const size_t index =
                            static_cast<size_t>(z) * resolution_x
                            + static_cast<size_t>(x);
                        if (!hit[index] || height > samples[index]) {
                            samples[index] = height;
                            hit[index] = 1u;
                        }
                    }
                }
            }

            fill_missing_projection_samples(
                samples,
                hit,
                resolution_x,
                resolution_y,
                terrain.min_height);

            float min_height = samples.empty() ? 0.0f : samples[0];
            float max_height = min_height;
            for (float height : samples) {
                min_height = (std::min)(min_height, height);
                max_height = (std::max)(max_height, height);
            }

            data.shape_kind = CollisionShapeKind::TerrainHeightField;
            data.mesh = terrain.mesh;
            data.source_triangle_count = terrain.mesh_triangle_count;
            data.accepted_triangle_count =
                terrain.mesh_accepted_surface_triangle_count;
            data.origin[0] = terrain.origin[0];
            data.origin[1] = terrain.origin[1];
            data.size[0] = terrain.size[0];
            data.size[1] = terrain.size[1];
            data.resolution_x = resolution_x;
            data.resolution_y = resolution_y;
            data.vertical_scale = 1.0f;
            data.base_height = 0.0f;
            data.min_height = min_height;
            data.max_height = max_height;
            data.height_samples = std::move(samples);
            data.supports_height_query = true;
            data.supports_ray_query = true;
            return true;
        }

        // Mapping describing how a raw scalar-field heightfield grid projects
        // into world space. World Y = value * vertical_scale + base_height.
        struct HeightFieldGridMapping
        {
            wz::asset::AssetKey height_field{};
            float origin[2]{ 0.0f, 0.0f };
            float size[2]{ 1.0f, 1.0f };
            float vertical_scale = 1.0f;
            float base_height = 0.0f;
            // Which grid the SOURCE and DESTINATION are both read on. Part of
            // the mapping because it is what makes a texel index a world
            // position -- see height_field_grid_span in collision.h.
            bool placement_driven = false;
        };

        float grid_height_sample_at(
            const std::vector<float>& values,
            uint32_t src_w,
            uint32_t src_h,
            float vertical_scale,
            float base_height,
            uint32_t x,
            uint32_t z) noexcept
        {
            const size_t index = static_cast<size_t>(z) * src_w + x;
            (void)src_h;
            return index < values.size()
                ? base_height + values[index] * vertical_scale
                : base_height;
        }

        float bilinear_grid_height_sample(
            const std::vector<float>& values,
            uint32_t src_w,
            uint32_t src_h,
            float vertical_scale,
            float base_height,
            float sample_x,
            float sample_z) noexcept
        {
            if (src_w == 0u || src_h == 0u) {
                return base_height;
            }
            const uint32_t x0 = static_cast<uint32_t>(std::floor(sample_x));
            const uint32_t z0 = static_cast<uint32_t>(std::floor(sample_z));
            const uint32_t x1 = (std::min)(x0 + 1u, src_w - 1u);
            const uint32_t z1 = (std::min)(z0 + 1u, src_h - 1u);
            const float tx = sample_x - static_cast<float>(x0);
            const float tz = sample_z - static_cast<float>(z0);

            const float h00 = grid_height_sample_at(
                values, src_w, src_h, vertical_scale, base_height, x0, z0);
            const float h10 = grid_height_sample_at(
                values, src_w, src_h, vertical_scale, base_height, x1, z0);
            const float h01 = grid_height_sample_at(
                values, src_w, src_h, vertical_scale, base_height, x0, z1);
            const float h11 = grid_height_sample_at(
                values, src_w, src_h, vertical_scale, base_height, x1, z1);
            const float h0 = h00 + (h10 - h00) * tx;
            const float h1 = h01 + (h11 - h01) * tx;
            return h0 + (h1 - h0) * tz;
        }

        // Resamples a raw heightfield grid (values + source resolution +
        // world mapping) into a CollisionAssetData heightfield with world Y
        // baked into height_samples (vertical_scale=1, base_height=0).
        bool resample_heightfield_grid(
            CollisionAssetData& data,
            const std::vector<float>& src_values,
            uint32_t src_w,
            uint32_t src_h,
            const HeightFieldGridMapping& mapping,
            uint32_t resolution_x,
            uint32_t resolution_y)
        {
            if (resolution_x < 2u
                || resolution_y < 2u
                || src_w == 0u
                || src_h == 0u)
            {
                return false;
            }

            // Destination texel -> source texel, through the convention BOTH
            // grids are read on: texel i sits at normalised u = i/span, so
            // source_coord = i * span_src / span_dst. That reduces to the old
            // (src-1)/(res-1) for a standalone field, to src/res for a
            // placement-driven one -- matching how the samples are read back --
            // and to the identity in either when res == src, which is the
            // native-resolution default and therefore unchanged for every
            // existing asset.
            const float span_src_x =
                height_field_grid_span(mapping.placement_driven, src_w);
            const float span_src_z =
                height_field_grid_span(mapping.placement_driven, src_h);
            const float span_dst_x =
                height_field_grid_span(mapping.placement_driven, resolution_x);
            const float span_dst_z =
                height_field_grid_span(mapping.placement_driven, resolution_y);

            std::vector<float> samples;
            samples.resize(static_cast<size_t>(resolution_x) * resolution_y);
            float min_height = std::numeric_limits<float>::infinity();
            float max_height = -std::numeric_limits<float>::infinity();
            for (uint32_t z = 0; z < resolution_y; ++z) {
                const float source_z =
                    static_cast<float>(z) * span_src_z / span_dst_z;
                for (uint32_t x = 0; x < resolution_x; ++x) {
                    const float source_x =
                        static_cast<float>(x) * span_src_x / span_dst_x;
                    const float height =
                        bilinear_grid_height_sample(
                            src_values,
                            src_w,
                            src_h,
                            mapping.vertical_scale,
                            mapping.base_height,
                            source_x,
                            source_z);
                    samples[static_cast<size_t>(z) * resolution_x + x] =
                        height;
                    min_height = (std::min)(min_height, height);
                    max_height = (std::max)(max_height, height);
                }
            }

            data.shape_kind = CollisionShapeKind::TerrainHeightField;
            data.height_field = mapping.height_field;
            data.origin[0] = mapping.origin[0];
            data.origin[1] = mapping.origin[1];
            data.size[0] = mapping.size[0];
            data.size[1] = mapping.size[1];
            data.resolution_x = resolution_x;
            data.resolution_y = resolution_y;
            data.vertical_scale = 1.0f;
            data.base_height = 0.0f;
            data.min_height = min_height;
            data.max_height = max_height;
            data.height_samples = std::move(samples);
            data.supports_height_query = true;
            data.supports_ray_query = true;
            return true;
        }

        bool resample_heightfield_terrain(
            CollisionAssetData& data,
            const TerrainAssetData& terrain,
            uint32_t resolution_x,
            uint32_t resolution_y)
        {
            HeightFieldGridMapping mapping{};
            mapping.height_field = terrain.height_field;
            mapping.origin[0] = terrain.origin[0];
            mapping.origin[1] = terrain.origin[1];
            mapping.size[0] = terrain.size[0];
            mapping.size[1] = terrain.size[1];
            mapping.vertical_scale = terrain.vertical_scale;
            mapping.base_height = terrain.base_height;
            return resample_heightfield_grid(
                data,
                terrain.height_samples,
                terrain.resolution_x,
                terrain.resolution_y,
                mapping,
                resolution_x,
                resolution_y);
        }

        CollisionAssetData collision_from_height_field(
            const CollisionFromHeightFieldCompileDesc& desc,
            const ScalarFieldData& field)
        {
            CollisionAssetData data{};
            data.source_kind = CollisionSourceKind::Terrain;
            data.source_asset = desc.height_field;
            data.geometry_asset = desc.height_field;
            data.occupancy = desc.occupancy;

            // projection_resolution downsamples the constraint grid; 0 means
            // "native field resolution". A heightfield needs >= 2 per axis to
            // bilinear-sample, so any value < 2 (incl. a stray 1) falls back to
            // native rather than producing a degenerate, invalid surface.
            const uint32_t resolution_x =
                desc.projection_resolution_x < 2u
                    ? field.width
                    : desc.projection_resolution_x;
            const uint32_t resolution_y =
                desc.projection_resolution_y < 2u
                    ? field.height
                    : desc.projection_resolution_y;

            HeightFieldGridMapping mapping{};
            mapping.height_field = desc.height_field;
            mapping.origin[0] = desc.origin[0];
            mapping.origin[1] = desc.origin[1];
            mapping.size[0] = desc.size[0];
            mapping.size[1] = desc.size[1];
            mapping.vertical_scale = desc.vertical_scale;
            mapping.base_height = desc.base_height;
            mapping.placement_driven = desc.placement_driven;

            const bool projected = resample_heightfield_grid(
                data,
                field.values,
                field.width,
                field.height,
                mapping,
                resolution_x,
                resolution_y);
            if (!projected) {
                data.source_asset = {};
                return data;
            }

            data.bounds_min[0] = data.origin[0];
            data.bounds_min[1] = data.min_height;
            data.bounds_min[2] = data.origin[1];
            data.bounds_max[0] = data.origin[0] + data.size[0];
            data.bounds_max[1] = data.max_height;
            data.bounds_max[2] = data.origin[1] + data.size[1];
            data.placement_driven = desc.placement_driven;
            data.render_lod_base_resolution = desc.render_lod_base_resolution;
            data.render_lod_level_count = desc.render_lod_level_count;
            data.render_lod_cell_size = desc.render_lod_cell_size;
            data.constrain_to_drawn_surface = desc.constrain_to_drawn_surface;
            build_collision_height_mips(data);
            return data;
        }

        CollisionAssetData collision_from_terrain(
            const CollisionFromTerrainCompileDesc& desc,
            const TerrainAssetData& terrain)
        {
            CollisionAssetData data{};
            data.source_kind = CollisionSourceKind::Terrain;
            data.source_asset = desc.terrain;
            data.geometry_asset = terrain.source_asset;
            data.occupancy = desc.occupancy;
            copy_bounds(
                data.bounds_min,
                data.bounds_max,
                terrain.bounds_min,
                terrain.bounds_max);

            if (desc.build_method == CollisionBuildMethod::Bounds) {
                data.shape_kind = CollisionShapeKind::Bounds;
                data.supports_overlap_query = true;
                return data;
            }

            if (desc.build_method
                == CollisionBuildMethod::TerrainProjectionHeightField)
            {
                // < 2 (incl. 0 = native) falls back to the terrain's native
                // resolution; a heightfield needs >= 2 per axis to sample.
                const uint32_t resolution_x =
                    desc.projection_resolution_x < 2u
                        ? terrain.resolution_x
                        : desc.projection_resolution_x;
                const uint32_t resolution_y =
                    desc.projection_resolution_y < 2u
                        ? terrain.resolution_y
                        : desc.projection_resolution_y;
                const bool projected =
                    terrain.representation
                        == TerrainRepresentationKind::HeightField
                    ? resample_heightfield_terrain(
                        data,
                        terrain,
                        resolution_x,
                        resolution_y)
                    : project_mesh_terrain_to_heightfield(
                        data,
                        terrain,
                        resolution_x,
                        resolution_y);
                if (projected) {
                    data.bounds_min[0] = data.origin[0];
                    data.bounds_min[1] = data.min_height;
                    data.bounds_min[2] = data.origin[1];
                    data.bounds_max[0] = data.origin[0] + data.size[0];
                    data.bounds_max[1] = data.max_height;
                    data.bounds_max[2] = data.origin[1] + data.size[1];
                } else {
                    data.source_asset = {};
                }
                return data;
            }

            if (terrain.representation == TerrainRepresentationKind::HeightField) {
                data.shape_kind = CollisionShapeKind::TerrainHeightField;
                data.height_field = terrain.height_field;
                data.origin[0] = terrain.origin[0];
                data.origin[1] = terrain.origin[1];
                data.size[0] = terrain.size[0];
                data.size[1] = terrain.size[1];
                data.resolution_x = terrain.resolution_x;
                data.resolution_y = terrain.resolution_y;
                data.vertical_scale = terrain.vertical_scale;
                data.base_height = terrain.base_height;
                data.min_height = terrain.min_height;
                data.max_height = terrain.max_height;
                data.height_samples = terrain.height_samples;
                data.supports_height_query = true;
                data.supports_ray_query = true;
                return data;
            }

            data.shape_kind = CollisionShapeKind::TerrainMeshSurface;
            data.mesh = terrain.mesh;
            data.source_triangle_count = terrain.mesh_triangle_count;
            data.accepted_triangle_count =
                terrain.mesh_accepted_surface_triangle_count;
            data.points.reserve(terrain.mesh_surface_points.size() / 3u);
            for (size_t i = 0; i + 2 < terrain.mesh_surface_points.size(); i += 3) {
                CollisionPoint point{};
                point.position[0] = terrain.mesh_surface_points[i + 0];
                point.position[1] = terrain.mesh_surface_points[i + 1];
                point.position[2] = terrain.mesh_surface_points[i + 2];
                data.points.push_back(point);
            }
            data.indices = terrain.mesh_surface_indices;
            build_triangle_bounds_and_grid(data);
            data.min_height = terrain.min_height;
            data.max_height = terrain.max_height;
            data.supports_ray_query = terrain.supports_ray_query;
            data.supports_height_query = terrain.supports_height_query;
            data.supports_overlap_query = !data.indices.empty();
            return data;
        }
    }

    void register_collision_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ScalarFieldTable& scalar_fields_table,
        MeshTable& mesh_table,
        TerrainAssetTable& terrain_table,
        CollisionAssetTable& collision_table,
        PlacementTable& placement_table,
        PlacedFieldTable& placed_field_table,
        ClipmapLatticeScheduleTable& clipmap_lattice_schedule_table,
        const wz::asset::AssetSystem* asset_system,
        const EngineAssetCacheSettings& cache_settings)
    {
        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCollisionFromMeshSchema,
            .output_type = kAssetTypeCollisionAsset,
            .input_ports = {
                { "mesh", kAssetTypeMesh },
            },
            .parameters = {
                {
                    .name = "build_method",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Build method",
                    .default_num = 1.0,
                    .options = kMeshCollisionBuildMethodOptions,
                },
                {
                    .name = "occupancy_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Occupancy",
                    .default_num =
                        static_cast<double>(CollisionOccupancyKind::Solid),
                    .options = kCollisionOccupancyKindOptions,
                },
                {
                    .name = "occupancy_blocks_movement",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Blocks movement",
                    .default_num = 1.0,
                },
                {
                    .name = "occupancy_queryable",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Queryable",
                    .default_num = 1.0,
                },
            },
            .compile = [&logger, &mesh_table, &collision_table](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                CollisionFromMeshCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<CollisionFromMeshCompileDesc>(&input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            collision_from_mesh_desc_from_params(
                                *params,
                                dep_nodes);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error("collision mesh missing compile desc");
                    return compile_failed_node(
                        input, "collision mesh missing compile desc");
                }
                if (dep_handles.size() != 1) {
                    logger.error("collision mesh requires one mesh dependency");
                    return compile_failed_node(
                        input, "collision mesh requires one mesh dependency");
                }

                const MeshData* mesh = mesh_table.get(dep_handles[0]);
                if (!mesh || !mesh->valid()) {
                    logger.error("collision mesh source is invalid");
                    return compile_failed_node(
                        input, "collision mesh source is invalid");
                }

                CollisionAssetData data = collision_from_mesh(*desc, *mesh);
                if (!data.valid()) {
                    logger.error("compiled mesh collision asset is invalid");
                    return compile_failed_node(
                        input, "compiled mesh collision asset is invalid");
                }

                wz::asset::ResourceHandle handle =
                    collision_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store mesh collision asset");
                    return compile_failed_node(
                        input, "failed to store mesh collision asset");
                }

                return compiled_collision_node(input, handle);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCollisionFromTerrainSchema,
            .output_type = kAssetTypeCollisionAsset,
            .input_ports = {
                { "terrain", kAssetTypeTerrain },
            },
            .parameters = {
                {
                    .name = "build_method",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Build method",
                    .default_num = 1.0,
                    .options = kTerrainCollisionBuildMethodOptions,
                },
                {
                    .name = "occupancy_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Occupancy",
                    .default_num =
                        static_cast<double>(
                            CollisionOccupancyKind::WalkableSurface),
                    .options = kCollisionOccupancyKindOptions,
                },
                {
                    .name = "occupancy_blocks_movement",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Blocks movement",
                    .default_num = 1.0,
                },
                {
                    .name = "occupancy_queryable",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Queryable",
                    .default_num = 1.0,
                },
                {
                    .name = "projection_resolution_x",
                    .type = wz::asset::ParamType::Int,
                    .label = "Projection resolution X",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
                {
                    .name = "projection_resolution_y",
                    .type = wz::asset::ParamType::Int,
                    .label = "Projection resolution Y",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
            },
            .compile = [&logger, &terrain_table, &collision_table, cache_settings](
                const wz::asset::AssetNode& input,
                std::span<const wz::asset::AssetNode* const> dep_nodes,
                std::span<const wz::asset::ResourceHandle> dep_handles)
                    -> wz::asset::AssetNode
            {
                CollisionFromTerrainCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<CollisionFromTerrainCompileDesc>(
                        &input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            collision_from_terrain_desc_from_params(
                                *params,
                                dep_nodes);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error("collision terrain missing compile desc");
                    return compile_failed_node(
                        input, "collision terrain missing compile desc");
                }
                if (dep_handles.size() != 1) {
                    logger.error(
                        "collision terrain requires one terrain dependency");
                    return compile_failed_node(
                        input,
                        "collision terrain requires one terrain dependency");
                }

                const TerrainAssetData* terrain =
                    terrain_table.get(dep_handles[0]);
                if (!terrain || !terrain->valid()) {
                    logger.error("collision terrain source is invalid");
                    return compile_failed_node(
                        input, "collision terrain source is invalid");
                }

                CollisionAssetData cached_data{};
                if (load_cached_terrain_collision_impl(
                        cache_settings,
                        input.key,
                        logger,
                        cached_data))
                {
                    wz::asset::ResourceHandle handle =
                        collision_table.add(std::move(cached_data));
                    if (!handle.valid()) {
                        logger.error(
                            "failed to store cached terrain collision asset");
                        return compile_failed_node(
                            input,
                            "failed to store cached terrain collision asset");
                    }
                    return compiled_collision_node(input, handle);
                }

                CollisionAssetData data =
                    collision_from_terrain(*desc, *terrain);
                if (!data.valid()) {
                    logger.error("compiled terrain collision asset is invalid");
                    return compile_failed_node(
                        input, "compiled terrain collision asset is invalid");
                }

                store_cached_terrain_collision(
                    cache_settings,
                    input.key,
                    data,
                    logger);

                wz::asset::ResourceHandle handle =
                    collision_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error("failed to store terrain collision asset");
                    return compile_failed_node(
                        input, "failed to store terrain collision asset");
                }

                return compiled_collision_node(input, handle);
            }
        });

        registry.register_compiler(wz::asset::AssetCompiler{
            .input_schema = kCollisionFromHeightFieldSchema,
            .output_type = kAssetTypeCollisionAsset,
            .input_ports = {
                { "height_field", kAssetTypeScalarField },
                // Optional world-space frame (issue #218 Phase 1). When
                // connected, the placement is authoritative for the collision's
                // origin / size(=extent.xz) / vertical_scale(=extent.y) /
                // base_height, overriding this recipe's own params. When absent,
                // behaviour is unchanged (fully back-compatible).
                {
                    "placement",
                    kAssetTypePlacement,
                    wz::asset::InputPortRequirement::Optional,
                },
                // Optional PlacedField (issue #223): one upstream bundling the
                // height field + its Placement — the same combiner the terrain
                // clipmap reads. When connected it supersedes the separate
                // scalar field + placement ports so the two consumers share one
                // frame and cannot drift.
                {
                    "placed_field",
                    kAssetTypePlacedField,
                    wz::asset::InputPortRequirement::Optional,
                },
                // Optional resolved clipmap LOD schedule. When connected it
                // supersedes the authored render_lod_* params, so the ray
                // reconstruction of the DRAWN surface reads the same (m, L)
                // the lattice mesh was built from instead of a hand-typed
                // copy. When absent, behaviour is unchanged.
                {
                    "schedule",
                    kAssetTypeClipmapLatticeSchedule,
                    wz::asset::InputPortRequirement::Optional,
                },
            },
            .parameters = {
                {
                    .name = "origin_x",
                    .type = wz::asset::ParamType::Float,
                    .label = "Origin X",
                    .default_num = 0.0,
                },
                {
                    .name = "origin_z",
                    .type = wz::asset::ParamType::Float,
                    .label = "Origin Z",
                    .default_num = 0.0,
                },
                {
                    .name = "size_x",
                    .type = wz::asset::ParamType::Float,
                    .label = "Size X",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100000.0,
                },
                {
                    .name = "size_z",
                    .type = wz::asset::ParamType::Float,
                    .label = "Size Z",
                    .default_num = 1.0,
                    .min = 0.0,
                    .max = 100000.0,
                },
                {
                    .name = "vertical_scale",
                    .type = wz::asset::ParamType::Float,
                    .label = "Vertical scale",
                    .default_num = 1.0,
                    .min = -100000.0,
                    .max = 100000.0,
                },
                {
                    .name = "base_height",
                    .type = wz::asset::ParamType::Float,
                    .label = "Base height",
                    .default_num = 0.0,
                    .min = -100000.0,
                    .max = 100000.0,
                },
                {
                    .name = "occupancy_kind",
                    .type = wz::asset::ParamType::Enum,
                    .label = "Occupancy",
                    .default_num =
                        static_cast<double>(
                            CollisionOccupancyKind::WalkableSurface),
                    .options = kCollisionOccupancyKindOptions,
                },
                {
                    .name = "occupancy_blocks_movement",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Blocks movement",
                    .default_num = 1.0,
                },
                {
                    .name = "occupancy_queryable",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Queryable",
                    .default_num = 1.0,
                },
                {
                    .name = "projection_resolution_x",
                    .type = wz::asset::ParamType::Int,
                    .label = "Projection resolution X",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
                {
                    .name = "projection_resolution_y",
                    .type = wz::asset::ParamType::Int,
                    .label = "Projection resolution Y",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
                {
                    .name = "render_lod_base_resolution",
                    .type = wz::asset::ParamType::Int,
                    .label = "Render LOD base resolution",
                    .default_num = 0,
                    .min = 0,
                    .max = 65536,
                },
                {
                    .name = "render_lod_level_count",
                    .type = wz::asset::ParamType::Int,
                    .label = "Render LOD level count",
                    .default_num = 0,
                    .min = 0,
                    .max = 64,
                },
                // Off by default: turning it on changes where actors stand, and
                // trades view-independent physics for agreement with the
                // picture. Authored per collision so the two can be compared
                // in one scene.
                {
                    .name = "constrain_to_drawn_surface",
                    .type = wz::asset::ParamType::Bool,
                    .label = "Constrain to drawn surface",
                },
            },
            .compile =
                [&logger, &scalar_fields_table, &collision_table,
                 &placement_table, &placed_field_table,
                 &clipmap_lattice_schedule_table, asset_system,
                 cache_settings](
                    const wz::asset::AssetNode& input,
                    std::span<const wz::asset::AssetNode* const> dep_nodes,
                    std::span<const wz::asset::ResourceHandle> dep_handles)
                        -> wz::asset::AssetNode
            {
                CollisionFromHeightFieldCompileDesc param_desc{};
                const auto* desc =
                    std::any_cast<CollisionFromHeightFieldCompileDesc>(
                        &input.meta);
                if (!desc) {
                    if (const auto* params =
                            std::any_cast<wz::asset::ParamBlock>(
                                &input.meta))
                    {
                        param_desc =
                            collision_from_height_field_desc_from_params(
                                *params,
                                dep_nodes);
                        desc = &param_desc;
                    }
                }
                if (!desc) {
                    logger.error(
                        "collision height field missing compile desc");
                    return compile_failed_node(
                        input, "collision height field missing compile desc");
                }

                // Locate dependencies by asset TYPE, not positional index: the
                // scalar field is required, the placement is an OPTIONAL second
                // port (issue #218). dep_nodes and dep_handles are parallel, in
                // DAG order, so we scan them together rather than assuming a
                // fixed count/order.
                const ScalarFieldData* field = nullptr;
                const PlacementData* placement = nullptr;
                const ClipmapLatticeScheduleData* schedule = nullptr;
                for (size_t i = 0;
                    i < dep_nodes.size() && i < dep_handles.size();
                    ++i)
                {
                    if (dep_nodes[i]->type == kAssetTypeScalarField) {
                        if (!field) {
                            field = scalar_fields_table.get(dep_handles[i]);
                        }
                    }
                    else if (dep_nodes[i]->type == kAssetTypePlacement) {
                        if (!placement) {
                            placement = placement_table.get(dep_handles[i]);
                        }
                    }
                    else if (dep_nodes[i]->type
                        == kAssetTypeClipmapLatticeSchedule)
                    {
                        if (!schedule) {
                            schedule = clipmap_lattice_schedule_table.get(
                                dep_handles[i]);
                        }
                    }
                }

                // A connected PlacedField (issue #223) SUPERSEDES the separate
                // scalar field + placement ports: it bundles both as ONE upstream
                // — the same combiner the terrain clipmap reads, so the visual
                // terrain and this collision cannot drift out of alignment. The
                // field + placement live one hop away (deps of the PlacedField),
                // so resolve them through the asset system.
                wz::asset::AssetKey placed_height_field_key{};
                for (const wz::asset::AssetNode* dep : dep_nodes) {
                    if (dep->type != kAssetTypePlacedField) {
                        continue;
                    }
                    const PlacedFieldData* placed = nullptr;
                    if (asset_system) {
                        if (const auto* compiled =
                                asset_system->find_compiled(dep->key))
                        {
                            placed = placed_field_table.get(compiled->handle);
                        }
                    }
                    if (!placed || !placed->valid()) {
                        logger.error(
                            "collision height field placed field did not "
                            "resolve");
                        return compile_failed_node(
                            input,
                            "collision height field placed field did not "
                            "resolve");
                    }
                    if (asset_system) {
                        if (const auto* fc =
                                asset_system->find_compiled(placed->field_key))
                        {
                            field = scalar_fields_table.get(fc->handle);
                        }
                        if (const auto* pc =
                                asset_system->find_compiled(
                                    placed->placement_key))
                        {
                            placement = placement_table.get(pc->handle);
                        }
                    }
                    placed_height_field_key = placed->field_key;
                    break;
                }

                if (!field || !field->valid()) {
                    logger.error(
                        "collision height field requires a valid scalar field "
                        "dependency");
                    return compile_failed_node(
                        input,
                        "collision height field requires a valid scalar field "
                        "dependency");
                }

                // A placement DEP overrides the recipe's own world mapping. The
                // placement is authoritative; the param-derived values are left
                // untouched when no placement is connected. Note the collision's
                // AssetKey already folds the placement dep (deps participate in
                // the key via key_to_dep_hash / combine_dep_hashes), so a
                // placement change re-compiles the collision.
                CollisionFromHeightFieldCompileDesc resolved_desc = *desc;
                if (!(placed_height_field_key == wz::asset::AssetKey{})) {
                    // The PlacedField supplies the height field key (there is no
                    // direct scalar-field dep to read it from).
                    resolved_desc.height_field = placed_height_field_key;
                }
                if (placement) {
                    if (!placement->valid()) {
                        logger.error(
                            "collision height field placement is invalid");
                        return compile_failed_node(
                            input,
                            "collision height field placement is invalid");
                    }
                    resolved_desc.origin[0] = placement->origin[0];
                    resolved_desc.origin[1] = placement->origin[2];
                    resolved_desc.size[0] = placement->extent[0];
                    resolved_desc.size[1] = placement->extent[2];
                    resolved_desc.vertical_scale = placement->extent[1];
                    resolved_desc.base_height = placement->base_height;
                }
                // A connected ClipmapLatticeSchedule SUPERSEDES the authored
                // render_lod_* params. Those describe how the clipmap DRAWS
                // this field — how many rings and how wide the innermost one
                // is — so they belong to the lattice, not to the collision,
                // and hand-typing them here is what let them fall out of step
                // with what is actually drawn (authored 64/6 against a real
                // 128/7). Connect the same schedule the lattice mesh consumes
                // and the reconstruction tracks the render by construction.
                if (schedule) {
                    if (!schedule->valid()) {
                        logger.error(
                            "collision height field clipmap lattice schedule "
                            "is invalid");
                        return compile_failed_node(
                            input,
                            "collision height field clipmap lattice schedule "
                            "is invalid");
                    }
                    resolved_desc.render_lod_base_resolution =
                        schedule->base_resolution;
                    resolved_desc.render_lod_level_count =
                        schedule->level_count;
                    resolved_desc.render_lod_cell_size = schedule->cell_size;
                }
                // A connected placement bakes world-frame mapping values into
                // the compiled data; flag it so the runtime does not compose
                // the carrying node's transform on top (issue #224), and so the
                // resample and the sampler agree on the grid.
                resolved_desc.placement_driven = (placement != nullptr);
                desc = &resolved_desc;
                if (field->depth != 1) {
                    logger.error(
                        "collision height field source must be 2D");
                    return compile_failed_node(
                        input, "collision height field source must be 2D");
                }

                CollisionAssetData cached_data{};
                if (load_cached_terrain_collision_impl(
                        cache_settings,
                        input.key,
                        logger,
                        cached_data))
                {
                    wz::asset::ResourceHandle handle =
                        collision_table.add(std::move(cached_data));
                    if (!handle.valid()) {
                        logger.error(
                            "failed to store cached height field "
                            "collision asset");
                        return compile_failed_node(
                            input,
                            "failed to store cached height field "
                            "collision asset");
                    }
                    return compiled_collision_node(input, handle);
                }

                CollisionAssetData data =
                    collision_from_height_field(*desc, *field);
                if (!data.valid()) {
                    const std::string reason =
                        "compiled height field collision asset is invalid "
                        "(source field "
                        + std::to_string(field->width) + "x"
                        + std::to_string(field->height)
                        + "; the height field must be at least 2x2)";
                    logger.error(reason);
                    return compile_failed_node(input, reason);
                }

                store_cached_terrain_collision(
                    cache_settings,
                    input.key,
                    data,
                    logger);

                wz::asset::ResourceHandle handle =
                    collision_table.add(std::move(data));
                if (!handle.valid()) {
                    logger.error(
                        "failed to store height field collision asset");
                    return compile_failed_node(
                        input, "failed to store height field collision asset");
                }

                return compiled_collision_node(input, handle);
            }
        });
    }

    bool load_cached_terrain_collision(
        const EngineAssetCacheSettings& cache,
        const wz::asset::AssetKey& key,
        wz::Logger& logger,
        CollisionAssetData& data)
    {
        return load_cached_terrain_collision_impl(cache, key, logger, data);
    }
}
