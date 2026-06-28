// src/engine/rendering/rhi_scene_renderer.cpp
//
// The RHI scene render path, lifted from the scene editor's working
// render_scene_rhi / ensure_rhi_renderable / realize_rhi_program so the app and
// editor share one wozzits-rhi implementation (no legacy dx12 submit).

#include <engine/rendering/rhi_scene_renderer.h>

#include <engine/assets/engine_asset_library.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/rhi_asset_identity.h>
#include <engine/assets/render_program/render_program.h>
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/scene/scene_asset_data.h>

#include <engine/rendering/clipmap_view.h>
#include <engine/rendering/rhi_mesh_bridge.h>
#include <engine/rendering/rhi_render_program_bridge.h>
#include <engine/rendering/rhi_shader_bridge.h>

#include <gpu/dx12/dx12_internal.h>

#include <math/mat4.h>

#include <wozzits/rhi/draw_encode.h>
#include <wozzits/rhi/geometry_view.h>
#include <wozzits/rhi/shader_module.h>
#include <wozzits/rhi/shader_resource_group_layout.h>

#include <d3d12.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ea = wz::engine::assets;

namespace wz::engine::rendering
{
    namespace
    {
        std::vector<float> tight_mesh_positions(const ea::MeshData& mesh)
        {
            std::vector<float> out;
            out.reserve(mesh.vertices.size() * 3u);
            for (const ea::MeshVertex& vertex : mesh.vertices) {
                out.push_back(vertex.position[0]);
                out.push_back(vertex.position[1]);
                out.push_back(vertex.position[2]);
            }
            return out;
        }

        // Recover the clipmap lattice's (base_resolution, level_count) from a
        // generated lattice mesh, independent of cell_size. The generator
        // (make_clipmap_lattice_mesh) tags every vertex with its LOD level in
        // position.y, and the level-0 solid block is an (m+1)x(m+1) vertex grid,
        // so:
        //   level_count    = max(position.y) + 1
        //   base_resolution = round(sqrt(#level-0 vertices)) - 1
        // The VS needs base_resolution to size each level's morph band from its
        // world half-extent (m/2)*c_L. Falls back to the sane defaults if the
        // mesh somehow has no level-0 vertices.
        struct ClipmapLatticeDims
        {
            uint32_t base_resolution = 8u;
            uint32_t level_count = 1u;
        };

        ClipmapLatticeDims infer_clipmap_lattice_dims(const ea::MeshData& mesh)
        {
            ClipmapLatticeDims dims{};
            uint32_t max_level = 0u;
            uint64_t level0_vertices = 0u;
            for (const ea::MeshVertex& vertex : mesh.vertices) {
                const float level_f = vertex.position[1];
                const uint32_t level = level_f > 0.0f
                    ? static_cast<uint32_t>(level_f + 0.5f)
                    : 0u;
                max_level = level > max_level ? level : max_level;
                if (level == 0u) {
                    ++level0_vertices;
                }
            }
            dims.level_count = max_level + 1u;
            if (level0_vertices > 0u) {
                const double side =
                    std::sqrt(static_cast<double>(level0_vertices));
                const long rounded = std::lround(side);
                if (rounded >= 2) {
                    dims.base_resolution =
                        static_cast<uint32_t>(rounded) - 1u;
                }
            }
            return dims;
        }

        void release_unrealized_pull_buffers(
            wz::rhi::GpuResourceRegistry& resources,
            wz::rhi::GpuResourceHandle positions,
            wz::rhi::GpuResourceHandle indices)
        {
            if (positions.valid()) {
                resources.release(positions);
            }
            if (indices.valid()) {
                resources.release(indices);
            }
            // No collect() here. This runs mid-frame, with no wait_idle, while
            // other renderables may still reference resources the GPU has not
            // finished. collect(UINT64_MAX) ignores the last-use timeline and
            // would destroy them immediately (the registry's timeline is not
            // wired — nothing calls touch() — so even collect(current) would be
            // unsafe). Worse, pull buffers are deduped by identity without
            // refcounting, so the handle released here may be shared with a
            // live renderable. release() alone is timeline-safe: it drops these
            // from identity lookup so a fresh realize rebuilds, and the buffers
            // are reclaimed by on_graph_changed's wait_idle-guarded collect.
        }

        const wz::asset::AssetSystem::RegistrationEntry* registration_entry_for(
            const ea::EngineAssetLibrary& assets, const wz::asset::AssetKey& key)
        {
            const auto registered_assets = assets.system().registered_assets();
            const auto entry = std::ranges::find_if(
                registered_assets,
                [&key](const wz::asset::AssetSystem::RegistrationEntry& candidate) {
                    return candidate.node.key == key;
                });
            return entry != registered_assets.end() ? &*entry : nullptr;
        }

        // A clipmap-landscape recipe also binds a resident height texture and
        // packs a per-frame view transform. Carried alongside the pull source so
        // ensure_renderable can bind the texture into the object SRG and stash
        // the data the per-frame constant packing needs.
        struct ClipmapBinding
        {
            wz::asset::AssetKey height_texture_key{};
            ea::ClipmapLandscapeRenderSettings settings{};
            uint32_t heightmap_width = 1;
            uint32_t heightmap_height = 1;
            // Lattice geometry resolution, recovered from the lattice MeshData
            // at realize time (the per-vertex level tags + level-0 vertex count).
            // The VS sizes each level's morph band from base_resolution.
            uint32_t base_resolution = 8;
        };

        // A gaussian-splat-cloud recipe (#208) has no pull mesh: the renderer
        // binds the resident decoded splat StructuredBuffer (published by the
        // splat compiler under rhi_asset_identity(key, "splat_cloud")) at the
        // SplatCloud semantic and records a non-indexed DrawInstanced of
        // 4 * splat_count vertices. Carried alongside the (empty) pull source so
        // ensure_renderable takes the splat branch.
        struct SplatCloudBinding
        {
            wz::asset::AssetKey splat_cloud_key{};
            ea::GaussianSplatCloudRenderSettings settings{};
            uint32_t splat_count = 0;
        };

        struct PullMeshSource
        {
            wz::asset::AssetKey mesh_key{};
            wz::asset::AssetKey program_key{};
            uint64_t buffer_identity = 0;

            // Set when the asset compiler has published GPU-resident pull buffers
            // for this source (gpu_sparse_mesh). The renderer binds those by the
            // identity the compiler used — rhi_asset_identity(resident_key,
            // "pull_positions"/"pull_indices") — instead of re-uploading CPU mesh
            // data, with counts from the resident asset (no CPU MeshData needed).
            std::optional<wz::asset::AssetKey> resident_key{};
            uint32_t                           vertex_count = 0;
            uint32_t                           index_count = 0;

            // Set for a clipmap-landscape recipe (height_texture_key present). The
            // geometry still rides the CPU-pull mesh_key path (the lattice); this
            // adds the resident height-texture binding + view-transform inputs.
            std::optional<ClipmapBinding> clipmap{};

            // Set for a gaussian-splat-cloud recipe (#208). When present, this
            // source has NO pull mesh — mesh_key is empty — and ensure_renderable
            // takes the splat branch instead of the mesh-pull SRG/geometry.
            std::optional<SplatCloudBinding> splat{};
        };

        std::optional<PullMeshSource> pull_mesh_source_for_renderable(
            const ea::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& renderable_key)
        {
            const ea::RhiRenderableRecipe* recipe =
                assets.renderables().get_rhi_renderable_recipe(
                    ea::RenderableAsset{ .output = renderable_key });
            if (!recipe) {
                return std::nullopt;
            }

            // Gaussian-splat-cloud geometry (#208): no pull mesh. The decoded
            // splat StructuredBuffer is asset-published resident; surface the
            // cloud key + settings + splat count here so ensure_renderable binds
            // it at the SplatCloud semantic and records the instanced-quad draw.
            if (!(recipe->gaussian_splat_cloud_key == wz::asset::AssetKey{})) {
                const ea::GaussianSplatCloudHandle cloud_handle =
                    assets.gaussian_splats().get_cloud(
                        ea::GaussianSplatCloudAsset{
                            .output = recipe->gaussian_splat_cloud_key });
                const ea::GaussianSplatCloudData* cloud =
                    assets.gaussian_splats().get_cloud_data(cloud_handle);
                if (!cloud || !cloud->valid()) {
                    return std::nullopt;
                }
                SplatCloudBinding binding;
                binding.splat_cloud_key = recipe->gaussian_splat_cloud_key;
                binding.settings = recipe->splat;
                binding.splat_count =
                    static_cast<uint32_t>(cloud->splat_count());
                return PullMeshSource{
                    .program_key = recipe->program_key,
                    .splat = binding,
                };
            }

            // GPU-resident geometry (gpu_sparse_mesh, #190): bind the asset-
            // published pull buffers by the identity the compiler used. Counts
            // and the CPU-upload fallback mesh come from the resident asset; the
            // buffers stay asset-owned (the renderer binds, never releases them).
            if (!(recipe->gpu_sparse_mesh_key == wz::asset::AssetKey{})) {
                const ea::GpuSparseMeshHandle sparse_handle =
                    assets.gpu_sparse_meshes().get_gpu_sparse_mesh(
                        ea::GpuSparseMeshAsset{
                            .output = recipe->gpu_sparse_mesh_key });
                const ea::GpuSparseMeshData* sparse =
                    assets.gpu_sparse_meshes().get_gpu_sparse_mesh_data(
                        sparse_handle);
                if (!sparse || !sparse->valid()) {
                    return std::nullopt;
                }
                return PullMeshSource{
                    .mesh_key = sparse->source_mesh_key,
                    .program_key = recipe->program_key,
                    .buffer_identity =
                        ea::rhi_asset_identity(recipe->gpu_sparse_mesh_key),
                    .resident_key = recipe->gpu_sparse_mesh_key,
                    .vertex_count = sparse->vertex_count,
                    .index_count = sparse->index_count,
                };
            }

            // Clipmap-landscape geometry: the lattice rides the CPU-pull mesh_key
            // path (uploaded + owned by the renderer, exactly like a plain pull
            // mesh); the recipe additionally names a resident height ScalarField
            // texture the VS samples. Surface that here so ensure_renderable binds
            // the texture into the object SRG and stashes the heightmap dims +
            // settings for the per-frame view-transform packing.
            std::optional<ClipmapBinding> clipmap{};
            if (!(recipe->height_texture_key == wz::asset::AssetKey{})) {
                ClipmapBinding binding;
                binding.height_texture_key = recipe->height_texture_key;
                binding.settings = recipe->clipmap;
                const ea::ScalarFieldHandle field_handle =
                    assets.scalar_fields().get_scalar_field(
                        ea::ScalarFieldAsset{ .output = recipe->height_texture_key });
                if (const ea::ScalarFieldData* field =
                        assets.scalar_fields().get_scalar_field_data(field_handle))
                {
                    binding.heightmap_width = field->width == 0u ? 1u : field->width;
                    binding.heightmap_height =
                        field->height == 0u ? 1u : field->height;
                }
                clipmap = binding;
            }

            // CPU pull-mesh geometry: the renderer uploads and owns the buffers.
            return PullMeshSource{
                .mesh_key = recipe->mesh_key,
                .program_key = recipe->program_key,
                .buffer_identity = ea::rhi_asset_identity(recipe->mesh_key),
                .clipmap = clipmap,
            };
        }

        // Compose an authored TRS into a column-major world matrix (translation
        // in m[12..14]), matching the MVP convention the pull program expects.
        wz::math::Mat4 world_from_transform(const ea::AuthoredTransform& t)
        {
            const float x = t.rotation_quat[0];
            const float y = t.rotation_quat[1];
            const float z = t.rotation_quat[2];
            const float w = t.rotation_quat[3];
            const float r00 = 1.0f - 2.0f * (y * y + z * z);
            const float r01 = 2.0f * (x * y - w * z);
            const float r02 = 2.0f * (x * z + w * y);
            const float r10 = 2.0f * (x * y + w * z);
            const float r11 = 1.0f - 2.0f * (x * x + z * z);
            const float r12 = 2.0f * (y * z - w * x);
            const float r20 = 2.0f * (x * z - w * y);
            const float r21 = 2.0f * (y * z + w * x);
            const float r22 = 1.0f - 2.0f * (x * x + y * y);

            wz::math::Mat4 m{};
            m.m[0] = r00 * t.scale[0];
            m.m[1] = r10 * t.scale[0];
            m.m[2] = r20 * t.scale[0];
            m.m[3] = 0.0f;
            m.m[4] = r01 * t.scale[1];
            m.m[5] = r11 * t.scale[1];
            m.m[6] = r21 * t.scale[1];
            m.m[7] = 0.0f;
            m.m[8] = r02 * t.scale[2];
            m.m[9] = r12 * t.scale[2];
            m.m[10] = r22 * t.scale[2];
            m.m[11] = 0.0f;
            m.m[12] = t.translation[0];
            m.m[13] = t.translation[1];
            m.m[14] = t.translation[2];
            m.m[15] = 1.0f;
            return m;
        }

        // Per-draw root constants for a clipmap-landscape renderable. Packed to
        // match the `Clipmap` cbuffer in resources/shaders/clipmap/clipmap_vs.hlsl
        // BYTE-FOR-BYTE. Every member is a 16-byte-aligned float4 group, so this
        // tightly-packed struct and the HLSL cbuffer (which also 16-byte-aligns
        // each float4) agree without padding gymnastics.
        //
        // Issue #207 repacked this to carry the PER-LEVEL snap inputs the VS
        // needs (camera world XZ + c0 + base_resolution + a view_snapped flag)
        // instead of one pre-snapped lattice translation. Still exactly 128
        // bytes / 32 dwords (binding_layout==2's "clipmap" value_count).
        //
        //   offset  field
        //   ------  ------------------------------------------------------------
        //      0     view_projection      (float4x4, column-major)
        //     64     snap_params          (xy = camera world XZ, z = c0,
        //                                   w = view_snapped flag (1 or 0))
        //     80     world_to_uv          (xy = scale, zw = offset)
        //     96     texel_and_vertical   (xy = texel world size,
        //                                   z = vertical_scale, w = base height)
        //    112     texel_dims_extent    (xy = texel dims as float,
        //                                   z = base_resolution, w = reserved)
        //   ------
        //    128 bytes = 32 dwords.
        struct ClipmapDrawConstants
        {
            float view_projection[16];
            float snap_params[4];
            float world_to_uv[4];
            float texel_and_vertical[4];
            float texel_dims_extent[4];
        };
        static_assert(sizeof(ClipmapDrawConstants) == 128,
            "clipmap root constants must be 128 bytes (32 dwords) to match the "
            "binding_layout==2 SRG and the HLSL Clipmap cbuffer");

        // Build the clipmap draw constants from the view-projection, the camera
        // world position, the realized renderable's authored settings, the
        // resident heightmap dimensions, and the lattice base_resolution
        // recovered from the mesh. view_projection is column-major in m[0..15]
        // (the same layout the MVP path uses).
        ClipmapDrawConstants make_clipmap_draw_constants(
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos,
            const ea::ClipmapLandscapeRenderSettings& settings,
            uint32_t heightmap_width,
            uint32_t heightmap_height,
            uint32_t base_resolution)
        {
            const wz::engine::rendering::ClipmapViewTransform view =
                wz::engine::rendering::compute_clipmap_view(
                    camera_world_pos.x,
                    camera_world_pos.z,
                    settings,
                    ea::ClipmapLatticeParams{
                        .level_count = 1u,
                        .base_resolution = base_resolution,
                        .cell_size = 1.0f,
                    },
                    heightmap_width,
                    heightmap_height);

            ClipmapDrawConstants out{};
            std::memcpy(out.view_projection, view_projection.m,
                sizeof(out.view_projection));

            // Per-level snap inputs. The VS reconstructs each level's world
            // placement + snap from the camera XZ and c0 (== lattice world
            // scale). When view_snapped is off (an arbitrary supplied static
            // mesh, #205) the VS skips per-level snapping and treats position.y
            // as real geometry: pass the flag so it branches.
            out.snap_params[0] = view.camera_world_xz[0];
            out.snap_params[1] = view.camera_world_xz[1];
            out.snap_params[2] = view.lattice_world_scale;  // c0
            out.snap_params[3] = view.view_snapped ? 1.0f : 0.0f;

            out.world_to_uv[0] = view.world_to_uv_scale[0];
            out.world_to_uv[1] = view.world_to_uv_scale[1];
            out.world_to_uv[2] = view.world_to_uv_offset[0];
            out.world_to_uv[3] = view.world_to_uv_offset[1];

            out.texel_and_vertical[0] = view.texel_world_size[0];
            out.texel_and_vertical[1] = view.texel_world_size[1];
            out.texel_and_vertical[2] = view.vertical_scale;
            out.texel_and_vertical[3] = view.base_height;

            out.texel_dims_extent[0] = static_cast<float>(heightmap_width);
            out.texel_dims_extent[1] = static_cast<float>(heightmap_height);
            out.texel_dims_extent[2] = view.base_resolution;
            out.texel_dims_extent[3] = 0.0f;
            return out;
        }

        // Per-draw root constants for a scalar-field point-cloud renderable
        // (#208, fix-2 sphere redesign). Packed to match the `SplatView` cbuffer
        // in resources/shaders/gaussian_splat/gaussian_splat_field_cloud_vs.hlsl
        // BYTE-FOR-BYTE: two column-major float4x4 (world, view_proj) then a
        // camera_and_diameter float4 (xyz = camera world pos, w = sphere world
        // diameter). The VS billboards a uniform world-diameter sphere per sample
        // facing the camera position, so it needs the camera world pos, not the
        // viewport. Still exactly 36 dwords / 144 bytes — the binding_layout==3
        // "splat_view" value_count is unchanged.
        struct SplatCloudDrawConstants
        {
            float world[16];
            float view_proj[16];
            float camera_and_diameter[4];
        };
        static_assert(sizeof(SplatCloudDrawConstants) == 144,
            "splat root constants must be 144 bytes (36 dwords) to match the "
            "binding_layout==3 SRG and the HLSL SplatView cbuffer");

        SplatCloudDrawConstants make_splat_cloud_draw_constants(
            const wz::math::Mat4& world,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos,
            float diameter)
        {
            SplatCloudDrawConstants out{};
            std::memcpy(out.world, world.m, sizeof(out.world));
            std::memcpy(out.view_proj, view_projection.m, sizeof(out.view_proj));
            out.camera_and_diameter[0] = camera_world_pos.x;
            out.camera_and_diameter[1] = camera_world_pos.y;
            out.camera_and_diameter[2] = camera_world_pos.z;
            out.camera_and_diameter[3] = diameter;
            return out;
        }
    }

    std::vector<wz::math::Mat4> compute_scene_node_world_transforms(
        std::span<const ea::SceneNodeAsset> nodes)
    {
        const std::size_t n = nodes.size();

        // node id -> index, so a node can resolve its parent within `nodes`.
        std::unordered_map<std::string, std::size_t> index_by_id;
        index_by_id.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            index_by_id.emplace(nodes[i].id, i);
        }

        // Parent index per node; n == no parent / dangling id / self-parent.
        std::vector<std::size_t> parent(n, n);
        for (std::size_t i = 0; i < n; ++i) {
            if (!nodes[i].parent_id.has_value()) {
                continue;
            }
            const auto it = index_by_id.find(*nodes[i].parent_id);
            if (it != index_by_id.end() && it->second != i) {
                parent[i] = it->second;
            }
        }

        std::vector<wz::math::Mat4> local(n);
        for (std::size_t i = 0; i < n; ++i) {
            local[i] = world_from_transform(nodes[i].local);
        }

        // Resolve world = parent_world * local, parents before children, without
        // recursion (deep chains can't overflow). state: 0 unvisited, 1
        // in-progress, 2 resolved. A parent cycle breaks cleanly: an in-progress
        // ancestor is never a usable parent, so the node falls back to its local.
        std::vector<wz::math::Mat4> world(n);
        std::vector<std::uint8_t> state(n, 0u);
        std::vector<std::size_t> chain;
        chain.reserve(n);
        for (std::size_t start = 0; start < n; ++start) {
            if (state[start] != 0u) {
                continue;
            }
            chain.clear();
            std::size_t cur = start;
            while (cur != n && state[cur] == 0u) {
                state[cur] = 1u;
                chain.push_back(cur);
                cur = parent[cur];
            }
            // Unwind highest-ancestor-first so each node's parent is already
            // resolved. The chain's terminator is a root (n), an already-resolved
            // node (shared ancestor), or an in-progress node (cycle).
            for (std::size_t k = chain.size(); k-- > 0;) {
                const std::size_t i = chain[k];
                const std::size_t p = parent[i];
                world[i] = (p != n && state[p] == 2u)
                    ? wz::math::mul(world[p], local[i])
                    : local[i];
                state[i] = 2u;
            }
        }
        return world;
    }

    // Effective (inherited) visibility: a node draws only if it AND every
    // ancestor is visible, so hiding a parent hides its whole subtree (notably a
    // scene-source host hiding its grafted children). Mirrors the parent-walk in
    // compute_scene_node_world_transforms; a dangling/cyclic parent falls back to
    // the node's own visibility (never spuriously hidden).
    std::vector<std::uint8_t> compute_scene_node_effective_visibility(
        std::span<const ea::SceneNodeAsset> nodes)
    {
        const std::size_t n = nodes.size();

        std::unordered_map<std::string, std::size_t> index_by_id;
        index_by_id.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            index_by_id.emplace(nodes[i].id, i);
        }

        std::vector<std::size_t> parent(n, n);
        for (std::size_t i = 0; i < n; ++i) {
            if (!nodes[i].parent_id.has_value()) {
                continue;
            }
            const auto it = index_by_id.find(*nodes[i].parent_id);
            if (it != index_by_id.end() && it->second != i) {
                parent[i] = it->second;
            }
        }

        // effective = node.visible AND parent.effective, resolved parents-first
        // (1 = effectively visible, 0 = hidden). Same cycle-safe chain unwind.
        std::vector<std::uint8_t> effective(n, 1u);
        std::vector<std::uint8_t> state(n, 0u);
        std::vector<std::size_t> chain;
        chain.reserve(n);
        for (std::size_t start = 0; start < n; ++start) {
            if (state[start] != 0u) {
                continue;
            }
            chain.clear();
            std::size_t cur = start;
            while (cur != n && state[cur] == 0u) {
                state[cur] = 1u;
                chain.push_back(cur);
                cur = parent[cur];
            }
            for (std::size_t k = chain.size(); k-- > 0;) {
                const std::size_t i = chain[k];
                const std::size_t p = parent[i];
                const bool parent_visible = (p == n || state[p] != 2u)
                    ? true
                    : effective[p] != 0u;
                effective[i] = (nodes[i].visible && parent_visible) ? 1u : 0u;
                state[i] = 2u;
            }
        }
        return effective;
    }

    RhiSceneRenderer::RhiSceneRenderer(EngineGpuContext& gpu, wz::Logger& logger)
        : gpu_(gpu)
        , logger_(logger)
        , ctx_()
        , cache_(gpu.device, gpu.programs, gpu.compute_programs, gpu.shaders)
        , recorder_(gpu.device, cache_, gpu.resources, gpu.backend)
    {
        forward_ = ctx_.passes.acquire("forward");
    }

    void RhiSceneRenderer::simulation_tick()
    {
    }

    void RhiSceneRenderer::on_graph_changed()
    {
        // The new graph may fix previously-broken renderables; always allow
        // re-realize (must happen before the early-return below, since a fully
        // broken outgoing graph realizes nothing yet still populated this set).
        failed_renderables_.clear();

        // First bind (nothing realized yet): nothing to release or invalidate,
        // and no need to flush the GPU. Keeps load_scene's initial bind cheap.
        if (realized_renderables_.empty() && realized_programs_.empty()
            && registered_shaders_.empty()
            && gpu_.resources.resident_count() == 0u)
        {
            return;
        }

        // The outgoing graph's pull buffers may still be referenced by frames
        // the GPU has not finished. The registry's deferred release reclaims on
        // a GPU timeline value, but that timeline is not yet wired (nothing
        // calls touch()), so flush here to guarantee the GPU is done before we
        // collect — then release + collect is unambiguously safe.
        wz::gpu::wait_idle(gpu_.device);

        for (auto& [key, renderable] : realized_renderables_) {
            (void)key;
            // Only release renderer-owned (CPU-upload) buffers. Resident
            // asset-published buffers are owned by the asset library and
            // released by its release_unregistered_rhi_resources on the same
            // swap (deferred, before this collect) — never by the renderer.
            if (!renderable.owns_buffers) {
                continue;
            }
            if (renderable.positions.valid()) {
                gpu_.resources.release(renderable.positions);
            }
            if (renderable.indices.valid()) {
                gpu_.resources.release(renderable.indices);
            }
        }

        // The GPU is idle after wait_idle, so every pending resource is past its
        // last-use timeline: reclaim them all now (UINT64_MAX = "all timelines
        // completed"). Without this the released buffers would linger resident.
        gpu_.resources.collect(UINT64_MAX);

        // Release the realized PSOs + root signatures. These are keyed by rhi
        // program Tag; leaving them resident leaks device objects across swaps,
        // and a re-realize must rebuild them against the new graph anyway.
        cache_.clear();

        // Release the recorder's cached SRV descriptor tables. They view the
        // outgoing graph's pull buffers (just released); keeping them leaks
        // descriptor-heap ranges across swaps and could reuse a table for a
        // recycled handle. Safe now: wait_idle above flushed the GPU.
        recorder_.release_cached_descriptor_tables();

        // NOTE: the shared rhi program / compute-program / shader registries are
        // intentionally NOT cleared here. They live on EngineGpuContext and the
        // asset compiler registers into them during resolve — which runs BEFORE
        // on_graph_changed in the bind path — so clearing here would wipe the
        // program the compiler just produced. The asset side owns their lifecycle
        // now: EngineAssetLibrary::reconcile_rhi_render_program_registries() runs
        // AFTER resolve and releases only the entries whose AssetKey left the live
        // set — survivors stay, so a same-content rebind keeps them. The semantic
        // registries are graph-independent and kept across swaps.

        realized_renderables_.clear();
        realized_programs_.clear();
        registered_shaders_.clear();
    }

    bool RhiSceneRenderer::ensure_shader_module(
        const wz::asset::AssetKey& shader_key)
    {
        if (auto it = registered_shaders_.find(shader_key);
            it != registered_shaders_.end())
        {
            return it->second;
        }

        // The shader compiler produces every shader's rhi ShaderModule under
        // shader_ref(key) during resolve (#193) — the renderer never compiles
        // shaders. Verify the module is present; the program PSO realize then
        // resolves its bytecode via resolve_program_bytecode.
        const bool ok =
            gpu_.shaders.find(wz::engine::rendering::shader_ref(shader_key))
                .valid();
        if (!ok) {
            logger_.error(
                "RhiSceneRenderer: shader module missing for program "
                "(not produced by the shader compiler)");
        }
        registered_shaders_[shader_key] = ok;
        return ok;
    }

    const RhiSceneRenderer::RealizedProgram* RhiSceneRenderer::realize_program(
        ea::EngineAssetLibrary& assets, const wz::asset::AssetKey& program_key)
    {
        if (auto it = realized_programs_.find(program_key);
            it != realized_programs_.end())
        {
            return it->second.program.valid() ? &it->second : nullptr;
        }

        // Find-then-fallback: prefer the rhi program the asset compiler produced
        // (registered under program_ref during resolve). Binding it skips the
        // render-time read-legacy / D3DCompile / convert / register path.
        const wz::rhi::Tag produced =
            gpu_.programs.find(wz::engine::rendering::program_ref(program_key));
        if (produced.valid()) {
            if (!cache_.realize(produced)) {
                logger_.error("RhiSceneRenderer: pipeline realize failed");
                return nullptr;
            }
            auto [it, inserted] = realized_programs_.try_emplace(program_key);
            it->second = RealizedProgram{ program_key, {}, {}, produced };
            (void)inserted;
            return &it->second;
        }

        // Fallback: bridge the legacy program at render time — builtin programs,
        // not-yet-migrated custom programs, or a custom program whose compile was
        // a cache hit on rebind (so the producer's lambda didn't re-register it).
        ++render_time_program_bridges_;

        const auto* program_entry = registration_entry_for(assets, program_key);
        if (!program_entry || program_entry->dep_keys.size() < 2u) {
            logger_.error("RhiSceneRenderer: program missing shader deps");
            return nullptr;
        }
        const wz::asset::AssetKey vertex_key = program_entry->dep_keys[0];
        const wz::asset::AssetKey pixel_key = program_entry->dep_keys[1];

        const wz::asset::ResourceHandle program_handle =
            assets.render_programs().get_render_program(
                ea::RenderProgramAsset{ .key = program_key });
        const ea::RenderProgramData* data =
            assets.render_programs().get_render_program_data(program_handle);
        if (!data) {
            logger_.error("RhiSceneRenderer: render program data unavailable");
            return nullptr;
        }

        if (!ensure_shader_module(vertex_key)
            || !ensure_shader_module(pixel_key))
        {
            return nullptr;
        }

        const auto converted = wz::engine::rendering::to_rhi_render_program_desc(
            *data, program_key, vertex_key, pixel_key,
            gpu_.descriptor_semantics, gpu_.constant_semantics);
        if (!converted) {
            logger_.error("RhiSceneRenderer: program bridge failed");
            return nullptr;
        }

        const wz::rhi::Tag tag = gpu_.programs.register_program(*converted);
        if (!tag.valid() || !cache_.realize(tag)) {
            logger_.error("RhiSceneRenderer: pipeline realize failed");
            return nullptr;
        }

        auto [it, inserted] = realized_programs_.try_emplace(program_key);
        it->second = RealizedProgram{ program_key, vertex_key, pixel_key, tag };
        (void)inserted;
        return &it->second;
    }

    RhiSceneRenderer::RealizedRenderable* RhiSceneRenderer::ensure_renderable(
        ea::EngineAssetLibrary& assets, const wz::asset::AssetKey& renderable_key)
    {
        if (auto it = realized_renderables_.find(renderable_key);
            it != realized_renderables_.end())
        {
            return &it->second;
        }
        // Already known-unrealizable for this graph: skip silently (no per-frame
        // retry/log). Cleared on the next graph swap.
        if (failed_renderables_.count(renderable_key) != 0u) {
            return nullptr;
        }

        auto source = pull_mesh_source_for_renderable(assets, renderable_key);
        if (!source) {
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }
        const RealizedProgram* program = realize_program(assets, source->program_key);
        if (!program) {
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }

        const wz::rhi::RenderProgramDesc* registered =
            gpu_.programs.get(program->program);
        const wz::rhi::ShaderResourceGroupLayout* slot2_layout =
            registered
                ? wz::rhi::find_shader_resource_group_layout(
                    registered->shader_resource_groups, 2)
                : nullptr;
        if (!slot2_layout) {
            logger_.error("RhiSceneRenderer: program missing object SRG slot 2");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }

        // ── Gaussian-splat-cloud branch (#208) ──────────────────────────────
        // No pull mesh: bind the resident decoded splat StructuredBuffer (asset-
        // owned, found by the identity the splat compiler published) at the
        // SplatCloud semantic and record a non-indexed instanced-quad draw
        // (vertex_count = 4 * splat_count). Self-contained: returns here without
        // touching the mesh-pull / clipmap path below.
        if (source->splat) {
            const SplatCloudBinding& splat = *source->splat;
            if (splat.splat_count == 0u) {
                logger_.error("RhiSceneRenderer: splat cloud has zero splats");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            const wz::rhi::GpuResourceHandle splat_buffer =
                gpu_.resources.find(wz::rhi::ResourceIdentity{
                    ea::rhi_asset_identity(splat.splat_cloud_key, "splat_cloud"),
                    {} });
            if (!splat_buffer.valid()) {
                logger_.error(
                    "RhiSceneRenderer: splat cloud buffer not resident");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }

            auto [sit, sinserted] =
                realized_renderables_.try_emplace(renderable_key);
            RealizedRenderable& srealized = sit->second;
            srealized.renderable_key = renderable_key;
            srealized.program = program->program;
            srealized.owns_buffers = false;  // buffer is asset-owned
            srealized.is_splat_cloud = true;
            srealized.splat_settings = splat.settings;
            srealized.object_srg.reset(*slot2_layout);

            const wz::rhi::Tag splat_cloud_semantic =
                gpu_.descriptor_semantics.find("splat_cloud");
            const bool ssrg_ok =
                srealized.object_srg.set(splat_cloud_semantic, splat_buffer)
                    .has_value();
            if (!ssrg_ok || !srealized.object_srg.satisfies(*slot2_layout)) {
                realized_renderables_.erase(sit);
                logger_.error(
                    "RhiSceneRenderer: splat object SRG build failed");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }

            // Non-indexed quad draw: 6 verts per splat (two self-contained
            // triangles), all in one DrawInstanced(6 * splat_count, 1, 0, 0). The
            // program is a TriangleList, so 6 (not 4) verts are required or the
            // triangles span adjacent splats and shatter. No index buffer, no
            // streams — the VS pulls from the SplatCloud SRV by SV_VertexID.
            wz::rhi::GeometryView geometry;
            geometry.vertex_count = splat.splat_count * 6u;

            const SplatCloudDrawConstants initial_splat{};
            wz::rhi::DrawPacketAllocator allocator;
            wz::rhi::DrawPacketBuilder builder =
                wz::rhi::DrawPacketBuilder::begin(allocator);
            builder
                .set_geometry(geometry)
                .set_root_constants(std::span<const uint8_t>{
                    reinterpret_cast<const uint8_t*>(&initial_splat),
                    sizeof(initial_splat) })
                .add_shader_resource_group(srealized.object_srg);
            if (!builder.add_draw_item(wz::rhi::DrawRequest{
                    forward_, srealized.program, nullptr,
                    wz::rhi::StreamBufferIndices{}, 0,
                    wz::rhi::DrawListMask::from(forward_) }))
            {
                realized_renderables_.erase(sit);
                logger_.error(
                    "RhiSceneRenderer: splat draw packet build failed");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            srealized.packet = builder.end();
            (void)sinserted;
            return &srealized;
        }

        // Prefer the asset-published GPU-resident pull buffers (gpu_sparse_mesh),
        // found by the identity the compiler used. Those are asset-owned, so the
        // renderer binds but never releases them. Fall back to a per-realize CPU
        // upload for sources without resident buffers (e.g. the rhi pull-mesh
        // recipe) — those the renderer owns and releases.
        wz::rhi::GpuResourceHandle positions_handle{};
        wz::rhi::GpuResourceHandle indices_handle{};
        bool owns_buffers = false;
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;

        if (source->resident_key) {
            positions_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(*source->resident_key, "pull_positions"),
                {} });
            indices_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(*source->resident_key, "pull_indices"),
                {} });
        }

        if (positions_handle.valid() && indices_handle.valid()) {
            // Resident path: bind asset-owned buffers; counts from the asset.
            index_count = source->index_count;
            vertex_count = source->vertex_count;
        }
        else {
            // CPU-upload fallback: renderer-owned buffers.
            const ea::MeshHandle mesh_handle = assets.meshes().get_mesh(
                ea::MeshAsset{ .output = source->mesh_key });
            const ea::MeshData* mesh =
                assets.meshes().get_mesh_data(mesh_handle);
            if (!mesh || !mesh->valid()) {
                logger_.error("RhiSceneRenderer: renderable mesh unavailable");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            const wz::rhi::Tag position_variant =
                ctx_.resource_variants.acquire("mesh.pull_positions");
            const wz::rhi::Tag index_variant =
                ctx_.resource_variants.acquire("mesh.pull_indices");
            const std::vector<float> positions = tight_mesh_positions(*mesh);
            positions_handle = acquire_pull_buffer(
                gpu_.resources, source->buffer_identity, position_variant,
                positions.data(), positions.size() * sizeof(float),
                3u * sizeof(float));
            indices_handle = acquire_pull_buffer(
                gpu_.resources, source->buffer_identity, index_variant,
                mesh->indices.data(),
                mesh->indices.size() * sizeof(uint32_t),
                sizeof(uint32_t));
            if (!positions_handle.valid() || !indices_handle.valid()) {
                logger_.error("RhiSceneRenderer: pull buffer upload failed");
                release_unrealized_pull_buffers(
                    gpu_.resources, positions_handle, indices_handle);
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            owns_buffers = true;
            index_count = mesh->index_count();
            vertex_count = mesh->vertex_count();

            // Recover the lattice resolution from the mesh's level tags so the
            // per-frame view-transform packing can size the morph band. Only the
            // clipmap recipe rides this CPU-pull path with a lattice mesh.
            if (source->clipmap) {
                source->clipmap->base_resolution =
                    infer_clipmap_lattice_dims(*mesh).base_resolution;
            }
        }

        // Clipmap-landscape: locate the resident height texture (#197) by the
        // identity the scalar-field compiler published it under. It is asset-
        // owned — bind it into the object SRG, never release it. Resolve this
        // BEFORE inserting the realized entry so a missing texture fails cleanly
        // without leaving a half-built renderable behind (and, on the CPU-upload
        // path, releases the lattice pull buffers we just acquired).
        wz::rhi::GpuResourceHandle height_texture_handle{};
        if (source->clipmap) {
            height_texture_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(
                    source->clipmap->height_texture_key, "field_texture"),
                {} });
            if (!height_texture_handle.valid()) {
                if (owns_buffers) {
                    release_unrealized_pull_buffers(
                        gpu_.resources, positions_handle, indices_handle);
                }
                logger_.error(
                    "RhiSceneRenderer: clipmap height texture not resident");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
        }

        auto [it, inserted] = realized_renderables_.try_emplace(renderable_key);
        RealizedRenderable& realized = it->second;
        realized.renderable_key = renderable_key;
        realized.program = program->program;
        realized.positions = positions_handle;
        realized.indices = indices_handle;
        realized.owns_buffers = owns_buffers;
        if (source->clipmap) {
            realized.is_clipmap = true;
            realized.clipmap_settings = source->clipmap->settings;
            realized.heightmap_width = source->clipmap->heightmap_width;
            realized.heightmap_height = source->clipmap->heightmap_height;
            realized.clipmap_base_resolution = source->clipmap->base_resolution;
            // level_count from the same mesh level tags, and the mesh's world
            // width along X — the lattice is WORLD-SIZED (its positions bake the
            // finest cell size in), so c0 is recovered at render time as
            // width_x / grid_extent (see compute_clipmap_placement), NOT from the
            // node scale. The lattice-mesh pointer above is out of scope here, so
            // re-resolve it by key (a cheap cache lookup); the defaults stand if
            // it is somehow unavailable.
            if (const ea::MeshData* lattice_mesh =
                    assets.meshes().get_mesh_data(assets.meshes().get_mesh(
                        ea::MeshAsset{ .output = source->mesh_key }))) {
                realized.clipmap_level_count =
                    infer_clipmap_lattice_dims(*lattice_mesh).level_count;
                realized.clipmap_mesh_width_x =
                    wz::engine::rendering::clipmap_lattice_mesh_width_x(
                        *lattice_mesh);
            }
        }
        realized.object_srg.reset(*slot2_layout);

        const wz::rhi::Tag pulled_positions =
            gpu_.descriptor_semantics.find("pulled_mesh_positions");
        const wz::rhi::Tag pulled_indices =
            gpu_.descriptor_semantics.find("pulled_mesh_indices");
        bool srg_ok =
            realized.object_srg.set(pulled_positions, realized.positions)
            && realized.object_srg.set(pulled_indices, realized.indices);
        if (srg_ok && realized.is_clipmap) {
            // Bind the resident R32 height texture at the scalar_field_texture
            // semantic — the third descriptor in the binding_layout==2 object
            // SRG. After this the 3-descriptor SRG satisfies its layout.
            const wz::rhi::Tag scalar_field_texture =
                gpu_.descriptor_semantics.find("scalar_field_texture");
            srg_ok = realized.object_srg.set(
                scalar_field_texture, height_texture_handle).has_value();
        }
        if (!srg_ok || !realized.object_srg.satisfies(*slot2_layout)) {
            if (realized.owns_buffers) {
                release_unrealized_pull_buffers(
                    gpu_.resources, realized.positions, realized.indices);
            }
            realized_renderables_.erase(it);
            logger_.error("RhiSceneRenderer: object SRG build failed");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }

        wz::rhi::GeometryView geometry;
        geometry.index_buffer = realized.indices;
        geometry.index_count = index_count;
        geometry.vertex_count = vertex_count;

        // Initial root constants sized to the program's block: 64-byte identity
        // MVP for the pull path, or a zeroed 128-byte clipmap block. render_scene
        // overwrites these every frame before recording, but sizing them to the
        // pipeline's root-constant dword_count up front keeps the packet
        // internally consistent (a size mismatch would make the recorder reject).
        const wz::math::Mat4 initial_mvp = wz::math::Mat4::identity();
        const ClipmapDrawConstants initial_clipmap{};
        const std::span<const uint8_t> initial_constants =
            realized.is_clipmap
                ? std::span<const uint8_t>{
                      reinterpret_cast<const uint8_t*>(&initial_clipmap),
                      sizeof(initial_clipmap) }
                : std::span<const uint8_t>{
                      reinterpret_cast<const uint8_t*>(initial_mvp.m),
                      sizeof(initial_mvp.m) };
        wz::rhi::DrawPacketAllocator allocator;
        wz::rhi::DrawPacketBuilder builder =
            wz::rhi::DrawPacketBuilder::begin(allocator);
        builder
            .set_geometry(geometry)
            .set_root_constants(initial_constants)
            .add_shader_resource_group(realized.object_srg);
        if (!builder.add_draw_item(wz::rhi::DrawRequest{
                forward_, realized.program, nullptr,
                wz::rhi::StreamBufferIndices{}, 0,
                wz::rhi::DrawListMask::from(forward_) }))
        {
            if (realized.owns_buffers) {
                release_unrealized_pull_buffers(
                    gpu_.resources, realized.positions, realized.indices);
            }
            realized_renderables_.erase(it);
            logger_.error("RhiSceneRenderer: draw packet build failed");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }
        realized.packet = builder.end();

        (void)inserted;
        return &realized;
    }

    bool RhiSceneRenderer::render_scene(
        std::span<const ea::SceneNodeAsset> nodes,
        ea::EngineAssetLibrary& assets,
        const wz::math::Mat4& view_projection,
        const wz::math::Vec3& camera_world_pos)
    {
        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(gpu_.device);
        if (!cmd) {
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            wz::gpu::dx12::internal::get_current_rtv(gpu_.device);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv =
            wz::gpu::dx12::internal::get_dsv(gpu_.device);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        const float w =
            static_cast<float>(wz::gpu::dx12::internal::get_width(gpu_.device));
        const float h =
            static_cast<float>(wz::gpu::dx12::internal::get_height(gpu_.device));
        D3D12_VIEWPORT viewport{ 0.0f, 0.0f, w, h, 0.0f, 1.0f };
        cmd->RSSetViewports(1, &viewport);
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
        cmd->RSSetScissorRects(1, &scissor);

        uint32_t recorded = 0;

        // Hierarchical world transforms: each node's local TRS composed with its
        // parent chain, so a renderable child follows its parent. The RHI path
        // had drawn every node at its own local transform; this restores
        // inheritance without resurrecting the legacy compile_scene renderer.
        // The clipmap branch ignores this by design (its lattice follows the
        // camera, not the node).
        const std::vector<wz::math::Mat4> node_worlds =
            compute_scene_node_world_transforms(nodes);
        // Inherited visibility: a hidden parent hides its whole subtree (e.g. a
        // scene-source host hiding its grafted children), not just itself.
        const std::vector<std::uint8_t> node_effective_visible =
            compute_scene_node_effective_visibility(nodes);

        for (const ea::SceneNodeAsset& node : nodes) {
            const std::size_t node_index =
                static_cast<std::size_t>(&node - nodes.data());
            if (!node_effective_visible[node_index] || !node.renderable_asset) {
                continue;
            }
            RealizedRenderable* realized =
                ensure_renderable(assets, *node.renderable_asset);
            if (!realized) {
                continue;
            }

            if (realized->is_clipmap) {
                // The lattice mesh is now WORLD-SIZED (its compiler bakes the
                // finest cell metres into the vertex positions), so the renderer
                // must NOT re-scale it. Derive the placement from the MESH + the
                // node TRANSLATION (not node scale): the finest cell c0 =
                // mesh_width_x / grid_extent, the terrain world footprint =
                // c0 * heightmap_dims (so node scale X/Z no longer sizes it — the
                // clipmap is world-absolute), and node translation places it
                // (XZ -> world origin, Y -> base height). Vertical scale is still
                // node.scale.y for now (a separate concern, see the flag below);
                // view_snapped carries through from the recipe. The lattice still
                // snaps to / follows the camera; node ROTATION is not applied.
                const ea::ClipmapLandscapeRenderSettings placement =
                    wz::engine::rendering::compute_clipmap_placement(
                        realized->clipmap_mesh_width_x,
                        ea::ClipmapLatticeParams{
                            .level_count = realized->clipmap_level_count,
                            .base_resolution =
                                realized->clipmap_base_resolution,
                            .cell_size = 1.0f,
                        },
                        realized->heightmap_width,
                        realized->heightmap_height,
                        node.local.translation,
                        node.local.scale,
                        realized->clipmap_settings.view_snapped);

                // One-shot diagnostic: the clipmap is WORLD-ABSOLUTE — its XZ
                // footprint is world_size = c0 * heightmap_dims (NOT node scale
                // X/Z); only node.translation (origin) and node.scale.y (vertical)
                // feed it. A collision-from-height-field on the same node uses the
                // FULL node transform, so to align a terrain-stick constraint set
                // the landscape node scale X/Z to this world_size (Y already
                // matches as the shared vertical scale).
                if (!clipmap_placement_logged_) {
                    clipmap_placement_logged_ = true;
                    logger_.info(
                        "clipmap placement: world_size=("
                        + std::to_string(placement.world_size[0]) + ", "
                        + std::to_string(placement.world_size[1])
                        + ") world_origin=("
                        + std::to_string(placement.world_origin[0]) + ", "
                        + std::to_string(placement.world_origin[1])
                        + ") vertical_scale=" + std::to_string(
                            placement.vertical_scale)
                        + " | node.scale=("
                        + std::to_string(node.local.scale[0]) + ", "
                        + std::to_string(node.local.scale[1]) + ", "
                        + std::to_string(node.local.scale[2])
                        + "). To align a collision constraint set node scale X/Z "
                          "= world_size (node scale X/Z does NOT size the clipmap).");
                }

                const ClipmapDrawConstants constants =
                    make_clipmap_draw_constants(
                        view_projection,
                        camera_world_pos,
                        placement,
                        realized->heightmap_width,
                        realized->heightmap_height,
                        realized->clipmap_base_resolution);
                const auto* bytes =
                    reinterpret_cast<const uint8_t*>(&constants);
                realized->packet.root_constants.assign(
                    bytes, bytes + sizeof(constants));
            }
            else if (realized->is_splat_cloud) {
                // The splat VS billboards a uniform world-diameter sphere per
                // sample facing the camera, so it needs world + view_proj
                // separate (not a pre-multiplied MVP) and the camera world
                // position. `world` is the node's composed world transform, so
                // the cloud is placed + sized entirely by the scene node (it
                // overlays the clipmap when given the matching transform).
                const wz::math::Mat4& world =
                    node_worlds[static_cast<std::size_t>(&node - nodes.data())];
                const SplatCloudDrawConstants constants =
                    make_splat_cloud_draw_constants(
                        world, view_projection, camera_world_pos,
                        realized->splat_settings.splat_size);
                const auto* bytes =
                    reinterpret_cast<const uint8_t*>(&constants);
                realized->packet.root_constants.assign(
                    bytes, bytes + sizeof(constants));
            }
            else {
                const wz::math::Mat4& world =
                    node_worlds[static_cast<std::size_t>(&node - nodes.data())];
                const wz::math::Mat4 mvp = wz::math::mul(view_projection, world);
                realized->packet.root_constants.assign(
                    reinterpret_cast<const uint8_t*>(mvp.m),
                    reinterpret_cast<const uint8_t*>(mvp.m) + sizeof(mvp.m));
            }

            wz::rhi::record_packet(realized->packet, forward_, recorder_);
            ++recorded;
        }

        if (recorded == 0) {
            return true;
        }
        if (!recorder_.ready()) {
            logger_.error("RhiSceneRenderer: recorder rejected a draw");
            return false;
        }
        return true;
    }
}
