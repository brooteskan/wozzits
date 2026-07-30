// src/engine/rendering/rhi_scene_renderer.cpp
//
// The RHI scene render path, lifted from the scene editor's working
// render_scene_rhi / ensure_rhi_renderable / realize_rhi_program so the app and
// editor share one wozzits-rhi implementation (no legacy dx12 submit).

#include <engine/rendering/rhi_scene_renderer.h>

#include <engine/assets/atmosphere/atmosphere.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/assets/mesh/mesh.h>
#include <engine/assets/puppet_program.h>
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
#include <array>
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

        std::vector<float> tight_mesh_normals(const ea::MeshData& mesh)
        {
            std::vector<float> out;
            out.reserve(mesh.vertices.size() * 3u);
            for (const ea::MeshVertex& vertex : mesh.vertices) {
                out.push_back(vertex.normal[0]);
                out.push_back(vertex.normal[1]);
                out.push_back(vertex.normal[2]);
            }
            return out;
        }

        std::vector<float> tight_mesh_uvs(const ea::MeshData& mesh)
        {
            std::vector<float> out;
            out.reserve(mesh.vertices.size() * 2u);
            for (const ea::MeshVertex& vertex : mesh.vertices) {
                out.push_back(vertex.uv[0]);
                out.push_back(vertex.uv[1]);
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
            wz::rhi::GpuResourceHandle indices,
            wz::rhi::GpuResourceHandle normals = {},
            wz::rhi::GpuResourceHandle uvs = {})
        {
            if (positions.valid()) {
                resources.release(positions);
            }
            if (indices.valid()) {
                resources.release(indices);
            }
            if (uvs.valid()) {
                resources.release(uvs);
            }
            if (normals.valid()) {
                resources.release(normals);
            }
            // No collect() here. This runs mid-frame, with no wait_idle, while
            // other renderables may still reference resources the GPU has not
            // finished — and pull buffers are deduped by identity without
            // refcounting, so the handle released here may be shared with a
            // live renderable. release() alone is the right move: it only drops
            // these from identity lookup (so a fresh realize rebuilds) and marks
            // them pending. Now that the timeline is wired, render_scene's
            // per-frame collect reclaims them once the GPU passes their last
            // touch — they no longer linger until the next graph swap.
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

        // A star-field recipe (#266) mirrors the splat cloud: no pull mesh, the
        // renderer binds the resident star point StructuredBuffer (published
        // under rhi_asset_identity(key, "star_catalog")) at the StarCatalog
        // semantic and records a non-indexed draw of 6 * star_count vertices.
        struct StarFieldBinding
        {
            wz::asset::AssetKey star_catalog_key{};
            ea::StarFieldRenderSettings settings{};
            uint32_t star_count = 0;
        };

        // A puppet source (inochi S2b): the puppet key + a borrowed pointer to its
        // compiled runtime data (source + ResidentPuppet draw metadata). Valid for
        // the ensure_renderable call that produced this source.
        struct PuppetBinding
        {
            wz::asset::AssetKey puppet_key{};
            const ea::PuppetData* data = nullptr;
        };

        // Place a puppet on a render target: fit its puppet-space bounds into the
        // target viewport (keeping aspect, small margin) and center it. Returns the
        // puppet->target 2D affine the VS applies before mapping to NDC. The viewport
        // is a PARAMETER, not a screen assumption -- the overlay passes the
        // backbuffer size, an S6/RTT pass passes the offscreen texture's size (#280),
        // recomputed per render from the target dimensions.
        wz::engine::assets::inochi::Affine2D puppet_target_placement(
            const std::array<float, 2>& bounds_min,
            const std::array<float, 2>& bounds_max,
            float viewport_w,
            float viewport_h)
        {
            const float pw = bounds_max[0] - bounds_min[0];
            const float ph = bounds_max[1] - bounds_min[1];
            float scale = 1.0f;
            if (pw > 0.0f && ph > 0.0f && viewport_w > 0.0f && viewport_h > 0.0f) {
                const float sx = viewport_w / pw;
                const float sy = viewport_h / ph;
                scale = (sx < sy ? sx : sy) * 0.9f;
            }
            const float pcx = 0.5f * (bounds_min[0] + bounds_max[0]);
            const float pcy = 0.5f * (bounds_min[1] + bounds_max[1]);

            wz::engine::assets::inochi::Affine2D m;
            m.a = scale; m.b = 0.0f; m.tx = 0.5f * viewport_w - scale * pcx;
            m.c = 0.0f;  m.d = scale; m.ty = 0.5f * viewport_h - scale * pcy;
            return m;
        }

        struct PullMeshSource
        {
            wz::asset::AssetKey mesh_key{};
            wz::asset::AssetKey program_key{};
            // Compositing order, copied from the recipe; drives the submit loop's
            // world -> overlay ordering.
            wz::engine::assets::DrawLayer draw_layer =
                wz::engine::assets::DrawLayer::World;
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

            // Set for a star-field recipe (#266). Same "no pull mesh" contract as
            // splat; ensure_renderable takes the star branch.
            std::optional<StarFieldBinding> star{};

            // Set for a puppet recipe (inochi S2b): no pull mesh; ensure_renderable
            // takes the puppet branch and records one packet per Part.
            std::optional<PuppetBinding> puppet{};

            // Baked mesh-style shading (issue #195 slice A). Carried from the
            // recipe; when style.has_style is set the renderer packs the 28-dword
            // MeshStyleDrawConstants (MVP + style) instead of the plain 16-float
            // MVP, and the program is expected to declare the "mesh_style" root
            // constant (binding_layout preset 4). Default = no style.
            ea::MeshRenderStyleShading style{};

            // Custom renderable recipe (issue #228): geometry rides the CPU
            // pull-mesh path above, every extra resource comes from walking
            // custom_bindings (identity = rhi_asset_identity(key, variant),
            // asset-owned), and the root-constant block is the head packer
            // named by custom_constants_head followed by the authored tail
            // values at their baked offsets. No per-recipe semantics here —
            // this is the generic bind the 0x70A compiler validated.
            bool custom = false;
            std::vector<ea::RhiRenderableBinding> custom_bindings;
            ea::RenderBindingConstantsHead custom_constants_head =
                ea::RenderBindingConstantsHead::None;
            uint32_t custom_constants_dwords = 0;
            std::vector<ea::RhiRenderableConstant> custom_constants;
        };

        // The clipmap/terrain binding a recipe carries (height_texture_key + the
        // world settings) plus the height field's resident texel dims — or
        // nullopt when the recipe names no height texture. Shared by the generic
        // custom (0x70A, CameraSnappedTerrain head, #233) and the bespoke clipmap
        // (0x708) paths so both get the identical per-frame terrain packing.
        std::optional<ClipmapBinding> clipmap_binding_for(
            const ea::EngineAssetLibrary& assets,
            const ea::RhiRenderableRecipe& recipe)
        {
            if (recipe.height_texture_key == wz::asset::AssetKey{}) {
                return std::nullopt;
            }
            ClipmapBinding binding;
            binding.height_texture_key = recipe.height_texture_key;
            binding.settings = recipe.clipmap;
            const ea::ScalarFieldHandle field_handle =
                assets.scalar_fields().get_scalar_field(
                    ea::ScalarFieldAsset{ .output = recipe.height_texture_key });
            if (const ea::ScalarFieldData* field =
                    assets.scalar_fields().get_scalar_field_data(field_handle))
            {
                binding.heightmap_width =
                    field->width == 0u ? 1u : field->width;
                binding.heightmap_height =
                    field->height == 0u ? 1u : field->height;
            }
            return binding;
        }

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

            // Custom renderable recipe (issue #228): CPU pull-mesh geometry
            // plus the compiled generic bindings/constants, carried verbatim
            // so ensure_renderable and the per-frame pack stay schema-blind.
            // A CameraSnappedTerrain-head recipe (#233) also names a height
            // texture, so it rides the SAME clipmap binding the 0x708 path uses
            // — realize then derives the mesh/field terrain data for the shared
            // per-frame packer. The height texture itself is bound generically
            // via custom_bindings (scalar_field_texture), not the is_clipmap SRG
            // branch, so there is no double-bind.
            if (recipe->custom) {
                return PullMeshSource{
                    .mesh_key = recipe->mesh_key,
                    .program_key = recipe->program_key,
                    .draw_layer = recipe->draw_layer,
                    .buffer_identity =
                        ea::rhi_asset_identity(recipe->mesh_key),
                    .clipmap = clipmap_binding_for(assets, *recipe),
                    .custom = true,
                    .custom_bindings = recipe->bindings,
                    .custom_constants_head = recipe->constants_head,
                    .custom_constants_dwords = recipe->constants_dwords,
                    .custom_constants = recipe->constants,
                };
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
                    .draw_layer = recipe->draw_layer,
                    .splat = binding,
                };
            }

            // Star-field geometry (#266): no pull mesh. The decoded star point
            // StructuredBuffer is asset-published resident; surface the catalog
            // key + settings + star count so ensure_renderable binds it at the
            // StarCatalog semantic and records the instanced-billboard draw.
            if (!(recipe->star_catalog_key == wz::asset::AssetKey{})) {
                const ea::StarCatalogHandle catalog_handle =
                    assets.star_catalogs().get_catalog(
                        ea::StarCatalogAsset{
                            .output = recipe->star_catalog_key });
                const wz::engine::starfield::StarCatalog* catalog =
                    assets.star_catalogs().get_catalog_data(catalog_handle);
                if (!catalog || catalog->stars.empty()) {
                    return std::nullopt;
                }
                StarFieldBinding binding;
                binding.star_catalog_key = recipe->star_catalog_key;
                binding.settings = recipe->star;
                binding.star_count =
                    static_cast<uint32_t>(catalog->stars.size());
                return PullMeshSource{
                    .program_key = recipe->program_key,
                    .draw_layer = recipe->draw_layer,
                    .star = binding,
                };
            }

            // Inochi2D puppet geometry (inochi S2b): no pull mesh. The puppet's
            // atlases + per-Part pull buffers are asset-published resident; carry
            // the key + the compiled data so ensure_renderable records one packet
            // per Part in the Overlay layer.
            if (!(recipe->puppet_key == wz::asset::AssetKey{})) {
                const ea::PuppetData* data =
                    assets.puppets().get_puppet_data(recipe->puppet_key);
                if (!data || data->resident.empty()) {
                    return std::nullopt;
                }
                PuppetBinding binding;
                binding.puppet_key = recipe->puppet_key;
                binding.data = data;
                return PullMeshSource{
                    .program_key = recipe->program_key,
                    .draw_layer = recipe->draw_layer,
                    .puppet = binding,
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
                    .draw_layer = recipe->draw_layer,
                    .buffer_identity =
                        ea::rhi_asset_identity(recipe->gpu_sparse_mesh_key),
                    .resident_key = recipe->gpu_sparse_mesh_key,
                    .vertex_count = sparse->vertex_count,
                    .index_count = sparse->index_count,
                };
            }

            // CPU pull-mesh geometry: the renderer uploads and owns the buffers.
            // Carry any baked mesh-style shading (issue #195 slice A) so the pack
            // path can emit the MVP + style root-constant block. A non-custom
            // recipe never carries a height texture (camera-snapped terrain is a
            // custom recipe now, #234), so there is no clipmap binding here.
            return PullMeshSource{
                .mesh_key = recipe->mesh_key,
                .program_key = recipe->program_key,
                .draw_layer = recipe->draw_layer,
                .buffer_identity = ea::rhi_asset_identity(recipe->mesh_key),
                .style = recipe->style,
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
        //                                   z = base_resolution,
        //                                   w = height mip count (#210))
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

        // Full mip count for the resident height texture (#210):
        // floor(log2(max(w,h)))+1. Kept in lock-step with
        // wz::gpu::max_mip_levels / full_mip_count in scalar_field_compilers.cpp
        // and the rhi #209 creation rule so the VS never samples past the chain.
        uint32_t height_texture_mip_count(uint32_t width, uint32_t height) noexcept
        {
            uint32_t largest = width > height ? width : height;
            uint32_t levels = 1u;
            while (largest > 1u) {
                largest >>= 1u;
                ++levels;
            }
            return levels;
        }

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
            uint32_t base_resolution,
            uint32_t level_count)
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
            // scale).
            //
            // snap_params.w carries the RING COUNT, with 0 meaning "not a
            // clipmap" -- an arbitrary supplied static mesh (#205), whose
            // position.y is real geometry and which skips per-level logic
            // entirely. It used to be a bare 0/1 flag; widening it to the count
            // costs nothing (the cbuffer is full at 32 dwords, and a float
            // carrying a boolean had the room) and lets the VS recognise its
            // OUTERMOST ring, which it otherwise cannot: without the count it
            // morphs that ring toward a coarser level that is never drawn.
            out.snap_params[0] = view.camera_world_xz[0];
            out.snap_params[1] = view.camera_world_xz[1];
            out.snap_params[2] = view.lattice_world_scale;  // c0
            out.snap_params[3] = view.view_snapped
                ? static_cast<float>(level_count < 1u ? 1u : level_count)
                : 0.0f;

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
            // #210: the resident height texture carries its full mip chain
            // (floor(log2(max(w,h)))+1). The VS samples mip == LOD level for
            // box-filtered coarse rings, clamped to this count. Matches
            // full_mip_count in scalar_field_compilers.cpp and the rhi #209
            // creation rule, so the sampled mip is always in range.
            out.texel_dims_extent[3] =
                static_cast<float>(
                    height_texture_mip_count(
                        heightmap_width, heightmap_height));
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

        // Per-draw root constants for a star field (#266). The splat block plus a
        // star_params float4 (x = authored intensity, rest spare for future dials
        // like twinkle). 40 dwords; matches the binding_layout==5 "star_view"
        // value_count and the StarView cbuffer in star_field_vs.hlsl. `world` is
        // the node's world transform: the VS rotates each star direction by it,
        // so the scene node orients the whole celestial sphere.
        struct StarFieldDrawConstants
        {
            float world[16];
            float view_proj[16];
            float camera_and_size[4];   // xyz = camera world pos, w = star_size
            float star_params[4];       // intensity, time, twinkle_amount, twinkle_speed
        };
        static_assert(sizeof(StarFieldDrawConstants) == 160,
            "star root constants must be 160 bytes (40 dwords) to match the "
            "binding_layout==5 SRG and the HLSL StarView cbuffer");

        StarFieldDrawConstants make_star_field_draw_constants(
            const wz::math::Mat4& world,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos,
            float star_size,
            float intensity,
            float time_seconds,
            float twinkle_amount,
            float twinkle_speed)
        {
            StarFieldDrawConstants out{};
            std::memcpy(out.world, world.m, sizeof(out.world));
            std::memcpy(out.view_proj, view_projection.m, sizeof(out.view_proj));
            out.camera_and_size[0] = camera_world_pos.x;
            out.camera_and_size[1] = camera_world_pos.y;
            out.camera_and_size[2] = camera_world_pos.z;
            out.camera_and_size[3] = star_size;
            out.star_params[0] = intensity;
            out.star_params[1] = time_seconds;
            out.star_params[2] = twinkle_amount;
            out.star_params[3] = twinkle_speed;
            return out;
        }

        // Per-draw root constants for a styled RHI pull mesh (issue #195 slice A).
        // Packed BYTE-FOR-BYTE to match the MeshStyle cbuffer in
        // mesh_style_pull_vs/ps.hlsl and the binding_layout==4 "mesh_style"
        // value_count (28 dwords / 112 bytes): the MVP the VS consumes, then the
        // 12 floats of baked shading the PS reads. style_params packs
        // (wireframe_emissive, surface_emissive, alpha, mode) where mode == 1
        // selects the wireframe layer, 0 the surface layer.
        struct MeshStyleDrawConstants
        {
            float mvp[16];
            float wireframe_color[4];
            float surface_color[4];
            float style_params[4];
        };
        static_assert(sizeof(MeshStyleDrawConstants) == 112,
            "mesh-style root constants must be 112 bytes (28 dwords) to match the "
            "binding_layout==4 SRG and the HLSL MeshStyle cbuffer");

        MeshStyleDrawConstants make_mesh_style_draw_constants(
            const wz::math::Mat4& mvp,
            const ea::MeshRenderStyleShading& style)
        {
            MeshStyleDrawConstants out{};
            std::memcpy(out.mvp, mvp.m, sizeof(out.mvp));
            for (int i = 0; i < 4; ++i) {
                out.wireframe_color[i] = style.wireframe_color[i];
                out.surface_color[i] = style.surface_color[i];
            }
            out.style_params[0] = style.wireframe_emissive;
            out.style_params[1] = style.surface_emissive;
            out.style_params[2] = style.alpha;
            // Prefer the wireframe layer's colour when it is the enabled layer;
            // otherwise shade with the surface layer. If both/neither are enabled
            // the surface layer wins (mode 0) — a deliberate, stable default.
            out.style_params[3] =
                (style.wireframe_enabled && !style.surface_enabled) ? 1.0f : 0.0f;
            return out;
        }
    }

    // The VIEW-frequency block (the struct + its offset table live in
    // rhi_scene_renderer.h, which is where the byte layout is pinned). Unlike
    // every packer above it this is per-FRAME, not per-draw, and it goes to the
    // shader through a one-element StructuredBuffer at register space 0 rather
    // than through root constants.
    ViewConstants make_view_constants(
        const wz::math::Vec3& camera_world_pos,
        const ea::AtmosphereData* atmosphere) noexcept
    {
        ViewConstants out{};

        // The observer, unconditionally. This is the half of the block that is
        // not fog: a program reading the camera from here must get it whether
        // or not anybody authored an atmosphere.
        out.camera[0] = camera_world_pos.x;
        out.camera[1] = camera_world_pos.y;
        out.camera[2] = camera_world_pos.z;
        out.camera[3] = 0.0f;   // unused; kept zero so the block is stable

        if (!atmosphere) {
            // No atmosphere authored for this frame. Zeros with enabled == 0 —
            // NOT a zero-density fog that still evaluates. wz_fog_from_view
            // returns early on this flag, so the shaded colour is untouched.
            return out;
        }

        out.fog_color[0] = atmosphere->fog_color[0];
        out.fog_color[1] = atmosphere->fog_color[1];
        out.fog_color[2] = atmosphere->fog_color[2];
        out.fog_color[3] = atmosphere->fog_density;

        out.fog_params[0] = atmosphere->fog_start_distance;
        out.fog_params[1] = atmosphere->fog_height_falloff;
        out.fog_params[2] = atmosphere->fog_enabled ? 1.0f : 0.0f;
        out.fog_params[3] = atmosphere->fog_saturation;
        return out;
    }

    ScreenConstants make_screen_constants(
        float viewport_width, float viewport_height) noexcept
    {
        ScreenConstants out{};
        out.viewport[0] = viewport_width;
        out.viewport[1] = viewport_height;
        out.viewport[2] = viewport_width  > 0.0f ? 1.0f / viewport_width  : 0.0f;
        out.viewport[3] = viewport_height > 0.0f ? 1.0f / viewport_height : 0.0f;
        return out;
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

    void merge_renderable_constant_overrides(
        std::span<uint8_t> block,
        std::span<const ea::RhiRenderableConstant> fields,
        std::span<const ea::SceneRenderableConstantOverride>
            overrides) noexcept
    {
        for (const ea::SceneRenderableConstantOverride& override_value :
             overrides)
        {
            for (const ea::RhiRenderableConstant& field : fields) {
                if (field.name != override_value.name) {
                    continue;
                }
                const size_t offset =
                    static_cast<size_t>(field.offset_dwords) * 4u;
                const size_t bytes =
                    static_cast<size_t>(
                        field.dwords > 4u ? 4u : field.dwords) * 4u;
                if (offset + bytes <= block.size()) {
                    std::memcpy(
                        block.data() + offset,
                        override_value.value,
                        bytes);
                }
                break;
            }
        }
    }

    RhiSceneRenderer::RhiSceneRenderer(EngineGpuContext& gpu, wz::Logger& logger)
        : gpu_(gpu)
        , logger_(logger)
        , ctx_()
        , cache_(gpu.device, gpu.programs, gpu.compute_programs, gpu.shaders,
                 &logger)
        , recorder_(gpu.device, cache_, gpu.resources, gpu.backend)
    {
        forward_ = ctx_.passes.acquire("forward");
    }

    void RhiSceneRenderer::simulation_tick(float dt_seconds)
    {
        // The app owns the frame boundary and already measures a real delta, so
        // the renderer's animation clock rides it (#282). Guard against a
        // negative or absurd delta (a debugger stop, a load hitch): animation
        // should resume, not teleport.
        constexpr float kMaxTickSeconds = 0.25f;
        if (dt_seconds > 0.0f) {
            animation_seconds_ += static_cast<double>(
                dt_seconds < kMaxTickSeconds ? dt_seconds : kMaxTickSeconds);
        }
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

        // Flush the GPU before swapping. The registry's timeline IS wired now
        // (render_scene touches every resolved resource and collects each
        // frame), so the pull-buffer releases below no longer NEED this stall to
        // be reclaimed safely. It stays for the caches that are NOT
        // timeline-tracked and are torn down on this same swap: the PSO cache
        // (cache_.clear()) and the recorder's descriptor-table cache
        // (release_cached_descriptor_tables()) — both may be referenced by
        // in-flight command lists, so the GPU must be idle before they are
        // released. With the GPU idle, the release + collect below is trivially
        // safe as well.
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
            if (renderable.normals.valid()) {
                gpu_.resources.release(renderable.normals);
            }
            if (renderable.uvs.valid()) {
                gpu_.resources.release(renderable.uvs);
            }
        }

        // Reclaim the just-released buffers. The GPU is idle (wait_idle above),
        // so its real completed value already covers every pending resource's
        // last-use timeline — pass that honest value rather than the UINT64_MAX
        // sledgehammer. Without this the released buffers would linger resident.
        gpu_.resources.collect(
            wz::gpu::completed_timeline_value(gpu_.device));

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

    wz::rhi::GpuResourceHandle RhiSceneRenderer::ensure_view_constants_buffer()
    {
        if (view_constants_buffer_.valid()) {
            return view_constants_buffer_;
        }

        // A one-element structured buffer: stride == the struct, so the SRV the
        // backend builds matches StructuredBuffer<WzViewConstants> and the
        // shader's view_constants[0] resolves. WriteOnce (not WriteFrequent):
        // that enum member has no backend implementation anywhere, and
        // WriteOnce is what GpuResourceRegistry::update actually requires — it
        // rejects only cpu_access == None. Anonymous identity (asset_id 0) on
        // purpose: this belongs to the renderer, not to any asset, so there is
        // no AssetKey to derive one from and nothing should find it by one.
        const wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::buffer(
            sizeof(ViewConstants),
            static_cast<uint32_t>(sizeof(ViewConstants)),
            wz::rhi::ResourceUsage_Sampled,
            wz::rhi::ResourceCpuAccess::WriteOnce);

        view_constants_buffer_ = gpu_.resources.acquire(desc);
        if (!view_constants_buffer_.valid()) {
            logger_.error(
                "RhiSceneRenderer: view constants buffer acquire failed");
            return {};
        }

        // Seed with the current frame's values. render_scene's per-frame
        // refresh already ran by the time a realize gets here, but it skipped
        // this buffer because it did not exist yet — without this the first
        // draw binding it would read the raw allocation.
        gpu_.resources.update(
            view_constants_buffer_, &view_constants_, sizeof(view_constants_));
        return view_constants_buffer_;
    }

    wz::rhi::GpuResourceHandle RhiSceneRenderer::ensure_screen_constants_buffer()
    {
        if (screen_constants_buffer_.valid()) {
            return screen_constants_buffer_;
        }

        // The screen-constants twin of ensure_view_constants_buffer: a one-element
        // structured buffer matching StructuredBuffer<WzScreenConstants>, anonymous
        // (renderer-owned, not an asset), WriteOnce so the per-frame refresh's
        // update() can write it.
        const wz::rhi::GpuResourceDesc desc = wz::rhi::GpuResourceDesc::buffer(
            sizeof(ScreenConstants),
            static_cast<uint32_t>(sizeof(ScreenConstants)),
            wz::rhi::ResourceUsage_Sampled,
            wz::rhi::ResourceCpuAccess::WriteOnce);

        screen_constants_buffer_ = gpu_.resources.acquire(desc);
        if (!screen_constants_buffer_.valid()) {
            logger_.error(
                "RhiSceneRenderer: screen constants buffer acquire failed");
            return {};
        }

        // Seed with the frame's values (the refresh skipped it while it did not
        // exist), so the first draw binding it does not read the raw allocation.
        gpu_.resources.update(
            screen_constants_buffer_, &screen_constants_,
            sizeof(screen_constants_));
        return screen_constants_buffer_;
    }

    bool RhiSceneRenderer::bind_view_constants(
        RealizedRenderable& realized,
        const wz::rhi::ShaderResourceGroupLayout* slot0_layout)
    {
        realized.has_view_srg = false;

        // Absent is the NORMAL case and not an error: no authored binding
        // layout declares a view head today, so every existing program lands
        // here and its packet gains nothing. Contrast the slot-2 lookup, which
        // logs and fails — an object group really is mandatory.
        if (!slot0_layout) {
            return true;
        }

        // The view head is EITHER the camera/fog block or the screen block, each
        // under its own descriptor semantic (a layout declares at most one). Bind
        // whichever the slot-0 layout actually asks for, and nothing when it asks
        // for neither -- keying off the SEMANTIC, not merely "has a space-0
        // group": register space 0 is also where legacy BuiltinRenderProgram
        // descs put their own root constants / SRVs, so a group that does not
        // declare the row could never be satisfied by binding this buffer and
        // would turn a working draw into a failed realize. Same shape as the
        // wants_normals test in ensure_renderable: bind only where the layout
        // asks for it.
        const wz::rhi::Tag view_semantic = gpu_.descriptor_semantics.find(
            ea::descriptor_semantic_name(ea::DescriptorSemantic::ViewConstants));
        const wz::rhi::Tag screen_semantic = gpu_.descriptor_semantics.find(
            ea::descriptor_semantic_name(ea::DescriptorSemantic::ScreenConstants));

        wz::rhi::Tag semantic;
        wz::rhi::GpuResourceHandle buffer;
        if (view_semantic.valid()
            && wz::rhi::find_descriptor_binding_index(
                   *slot0_layout, view_semantic))
        {
            semantic = view_semantic;
            buffer = ensure_view_constants_buffer();
        }
        else if (screen_semantic.valid()
            && wz::rhi::find_descriptor_binding_index(
                   *slot0_layout, screen_semantic))
        {
            semantic = screen_semantic;
            buffer = ensure_screen_constants_buffer();
        }
        else {
            return true;
        }

        if (!buffer.valid()) {
            logger_.error(
                "RhiSceneRenderer: view/screen constants unavailable for a "
                "program that declares a view block");
            return false;
        }

        realized.view_srg.reset(*slot0_layout);
        if (!realized.view_srg.set(semantic, buffer).has_value()
            || !realized.view_srg.satisfies(*slot0_layout))
        {
            logger_.error("RhiSceneRenderer: view SRG build failed");
            return false;
        }
        realized.has_view_srg = true;
        return true;
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
        const std::size_t program_dep_count =
            program_entry ? program_entry->dep_keys.size() : 0u;
        if (!program_entry || program_dep_count < 2u) {
            // Name which REQUIRED shader ports are unwired so the log is
            // actionable: port 0 = vertex, port 1 = pixel. dep_keys is
            // port-indexed, so an unwired port is either missing (index beyond
            // the array) or an empty key. NOTE this resolves the COMMITTED graph,
            // so a program that looks wired in the editor lands here when the
            // edit was not committed or a rewired node kept a stale key.
            std::string missing;
            for (std::size_t port = 0; port < 2u; ++port) {
                const bool wired =
                    port < program_dep_count
                    && program_entry->dep_keys[port] != wz::asset::AssetKey{};
                if (!wired) {
                    if (!missing.empty()) {
                        missing += ", ";
                    }
                    missing += (port == 0u ? "port 0 (vertex shader)"
                                           : "port 1 (pixel shader)");
                }
            }
            logger_.error(
                "RhiSceneRenderer: render program missing shader deps — "
                + std::to_string(program_dep_count)
                + " input(s) wired, unwired required: " + missing
                + " (resolved from the committed graph)");
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
        // The VIEW-frequency group, looked up exactly the same way but at the
        // view register space (0). nullptr means the program's binding layout
        // declares nothing there — the case for every program that exists —
        // and is handled silently by bind_view_constants below, which also
        // checks the group actually declares the view_constants row.
        const wz::rhi::ShaderResourceGroupLayout* slot0_layout =
            registered
                ? wz::rhi::find_shader_resource_group_layout(
                    registered->shader_resource_groups, 0)
                : nullptr;

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
            srealized.draw_layer = source->draw_layer;
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
            if (!bind_view_constants(srealized, slot0_layout)) {
                realized_renderables_.erase(sit);
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
            if (srealized.has_view_srg) {
                builder.add_shader_resource_group(srealized.view_srg);
            }
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

        // ── Puppet branch (inochi S2b) ──────────────────────────────────────
        // No pull mesh; N packets, one per Part. Each Part binds its resident
        // interleaved-vertex / index / atlas by identity into an object SRG, and
        // its PuppetPartBlock (a 2D affine placement + opacity + tints) as root
        // constants.
        // All Parts share one Screen view head (the viewport). Parts are already
        // draw-ordered (back-to-front by zsort) in the ResidentPuppet.
        if (source->puppet) {
            const wz::engine::assets::inochi::ResidentPuppet& puppet =
                source->puppet->data->resident;

            auto [pit, pinserted] =
                realized_renderables_.try_emplace(renderable_key);
            RealizedRenderable& prealized = pit->second;
            prealized.renderable_key = renderable_key;
            prealized.draw_layer = source->draw_layer;
            prealized.program = program->program;
            prealized.owns_buffers = false;  // buffers are asset-owned
            prealized.is_puppet = true;

            // Deform/physics runtime (S3c/S7): borrow the source puppet (stable
            // while realized) and seed the parameter + pendulum state. render_scene
            // drives it per frame.
            prealized.puppet_source = &source->puppet->data->source;
            prealized.puppet_params =
                wz::engine::assets::inochi::make_default_params(
                    *prealized.puppet_source);
            prealized.puppet_physics =
                wz::engine::assets::inochi::make_puppet_physics(
                    *prealized.puppet_source);
            // Deform baseline at default params: the per-frame pose is taken
            // relative to this so default params render the base (rest) mesh.
            prealized.puppet_deform_baseline =
                wz::engine::assets::inochi::evaluate_puppet_deform(
                    *prealized.puppet_source, prealized.puppet_params);

            // The shared Screen view head (viewport), bound once and reused by
            // every Part packet.
            if (!bind_view_constants(prealized, slot0_layout)) {
                realized_renderables_.erase(pit);
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }

            const wz::rhi::Tag verts_semantic =
                gpu_.descriptor_semantics.find("puppet_vertices");
            const wz::rhi::Tag idx_semantic =
                gpu_.descriptor_semantics.find("puppet_indices");
            const wz::rhi::Tag atlas_semantic =
                gpu_.descriptor_semantics.find("puppet_atlas");
            const wz::rhi::Tag mask_semantic =
                gpu_.descriptor_semantics.find("puppet_mask");

            // Mask compositing (#275). Every Part declares the mask row, so an
            // unmasked one binds the puppet's 1x1 white texture and its shader
            // test passes everywhere. The real mask TARGETS are target-sized and
            // built in the prepass; realize records which Parts want one and
            // seeds them all with white, so a puppet is drawable (unmasked)
            // before the first prepass and stays drawable if one ever fails.
            const wz::rhi::GpuResourceHandle no_mask =
                gpu_.resources.find(puppet.no_mask);
            if (!no_mask.valid()) {
                realized_renderables_.erase(pit);
                logger_.error(
                    "RhiSceneRenderer: puppet no-mask texture not resident");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            // The puppet's ONE shared vertex/index pair (#278): found once and
            // bound into every Part's SRG, so the Parts differ only by atlas and
            // mask and can share descriptor tables.
            const wz::rhi::GpuResourceHandle verts =
                gpu_.resources.find(puppet.vertices);
            const wz::rhi::GpuResourceHandle indices =
                gpu_.resources.find(puppet.indices);
            if (!verts.valid() || !indices.valid()) {
                realized_renderables_.erase(pit);
                logger_.error(
                    "RhiSceneRenderer: puppet shared pull buffers not resident");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            prealized.puppet_vertices = verts;
            prealized.puppet_vertex_scratch.assign(puppet.vertex_count, {});

            prealized.puppet_mask_targets.clear();
            prealized.puppet_mask_size = { 0u, 0u };
            prealized.puppet_part_mask_target.clear();
            prealized.puppet_part_mask_target.reserve(puppet.parts.size());

            // Per-Part blend (#274). Blend state lives in the PSO and a
            // DrawRequest carries ONE program tag, so a Part's authored blend
            // mode selects among the engine-provisioned puppet program VARIANTS
            // (identical shaders + SRG, different blend state). Resolve each
            // variant's tag once here; a variant that is absent (a graph
            // authored before #274) or fails to realize falls back to the bound
            // program, i.e. the pre-#274 behaviour of drawing everything Normal.
            const ea::PuppetProgramVariants puppet_variants =
                ea::puppet_program_variants(
                    assets.system(), assets.render_programs(),
                    source->program_key);
            std::array<wz::rhi::Tag, ea::kPuppetProgramBlendCount>
                puppet_variant_programs{};
            for (std::size_t vi = 0; vi < puppet_variant_programs.size(); ++vi) {
                const wz::asset::AssetKey& vkey =
                    puppet_variants.key_for(
                        static_cast<ea::PuppetProgramBlend>(vi));
                const RealizedProgram* vprogram =
                    vkey == source->program_key
                        ? program
                        : realize_program(assets, vkey);
                puppet_variant_programs[vi] =
                    vprogram ? vprogram->program : program->program;
            }

            // Fit + center the puppet in the current viewport (screen-space for
            // S2). The per-Part packet build below reads only the ResidentPuppet,
            // the program/layout, the shared view SRG, this placement, and the
            // pass -- no backbuffer assumption -- so S6/RTT reuses it verbatim.
            prealized.puppet_bounds_min = puppet.bounds_min;
            prealized.puppet_bounds_max = puppet.bounds_max;
            // Initial placement for the realize-time packet build; render_scene
            // recomputes it per frame for the actual target (#280).
            const wz::engine::assets::inochi::Affine2D puppet_to_target =
                puppet_target_placement(
                    puppet.bounds_min, puppet.bounds_max,
                    screen_constants_.viewport[0],
                    screen_constants_.viewport[1]);

            // Reserve so the per-Part SRG addresses the packets capture stay valid.
            prealized.puppet_part_srgs.reserve(puppet.parts.size());
            prealized.puppet_packets.reserve(puppet.parts.size());
            prealized.puppet_part_runtime.reserve(puppet.parts.size());
            for (const wz::engine::assets::inochi::ResidentPuppetPart& part :
                 puppet.parts) {
                const wz::rhi::GpuResourceHandle atlas =
                    part.atlas < puppet.atlases.size()
                        ? gpu_.resources.find(puppet.atlases[part.atlas])
                        : wz::rhi::GpuResourceHandle{};
                if (!atlas.valid()) {
                    realized_renderables_.erase(pit);
                    logger_.error(
                        "RhiSceneRenderer: puppet Part atlas not resident");
                    failed_renderables_.insert(renderable_key);
                    return nullptr;
                }

                prealized.puppet_part_srgs.emplace_back();
                wz::rhi::ShaderResourceGroup& part_srg =
                    prealized.puppet_part_srgs.back();
                part_srg.reset(*slot2_layout);
                const bool srg_ok =
                    part_srg.set(verts_semantic, verts).has_value()
                    && part_srg.set(idx_semantic, indices).has_value()
                    && part_srg.set(atlas_semantic, atlas).has_value()
                    && part_srg.set(mask_semantic, no_mask).has_value()
                    && part_srg.satisfies(*slot2_layout);
                if (!srg_ok) {
                    realized_renderables_.erase(pit);
                    logger_.error(
                        "RhiSceneRenderer: puppet Part object SRG build failed");
                    failed_renderables_.insert(renderable_key);
                    return nullptr;
                }

                // PuppetPartBlock (20 dwords): a 2D affine (Part-local px ->
                // screen px) row-packed + opacity, the Part's multiply tint and
                // screen tint (#276), then its mask params (#275). affine =
                // puppet->target o Part placement.
                //
                // An UNMASKED Part gets threshold 0: bound against the 1x1 white
                // texture, step(0, 1) passes everywhere, so the mask folds out.
                const wz::engine::assets::inochi::Affine2D m =
                    wz::engine::assets::inochi::compose(
                        puppet_to_target, part.placement);
                const bool part_masked =
                    part.mask_source
                    != wz::engine::assets::inochi::PuppetPartDraw::kNoMaskSource;
                std::array<std::uint32_t, 24> block{};
                const float head[20] = {
                    m.a, m.b, m.tx, part.opacity,
                    m.c, m.d, m.ty, 0.0f,
                    part.tint[0], part.tint[1], part.tint[2], 0.0f,
                    part.screen_tint[0], part.screen_tint[1],
                    part.screen_tint[2], 0.0f,
                    part_masked ? part.mask_threshold : 0.0f,
                    (part_masked && part.mask_inverted) ? 1.0f : 0.0f,
                    0.0f, 0.0f,
                };
                std::memcpy(block.data(), head, sizeof(head));
                // Row 5 is UINTs, not floats: the shared-buffer pull bases.
                block[20] = part.vertex_base;
                block[21] = part.index_base;

                wz::rhi::GeometryView geometry;
                geometry.vertex_count = part.index_count;  // VS pulls indices[vid]

                wz::rhi::DrawPacketAllocator allocator;
                wz::rhi::DrawPacketBuilder builder =
                    wz::rhi::DrawPacketBuilder::begin(allocator);
                builder
                    .set_geometry(geometry)
                    .set_root_constants(std::span<const uint8_t>{
                        reinterpret_cast<const uint8_t*>(block.data()),
                        block.size() * sizeof(std::uint32_t) })
                    .add_shader_resource_group(part_srg);
                if (prealized.has_view_srg) {
                    builder.add_shader_resource_group(prealized.view_srg);
                }
                // The Part draws through its blend variant's program (#274).
                const wz::rhi::Tag part_program = puppet_variant_programs[
                    static_cast<std::size_t>(
                        ea::puppet_program_blend_for_part(part.blend))];
                if (!builder.add_draw_item(wz::rhi::DrawRequest{
                        forward_, part_program, nullptr,
                        wz::rhi::StreamBufferIndices{}, 0,
                        wz::rhi::DrawListMask::from(forward_) }))
                {
                    realized_renderables_.erase(pit);
                    logger_.error(
                        "RhiSceneRenderer: puppet Part draw packet build failed");
                    failed_renderables_.insert(renderable_key);
                    return nullptr;
                }
                prealized.puppet_packets.push_back(builder.end());
                // Cache the WriteFrequent vertex handle + node index for the
                // per-frame deform update (parallel to puppet_packets).
                prealized.puppet_part_runtime.push_back(
                    RealizedRenderable::PuppetPartRuntime{
                        part.node_index, part.vertex_base, part.index_base,
                        part.vertex_count });
            }

            // Resolve the mask wiring now that every Part has a packet (#275).
            // parts and puppet_packets are 1:1 -- a Part that could not be built
            // fails the whole realize above -- so a Part's index is its packet's.
            {
                using PartDraw = wz::engine::assets::inochi::PuppetPartDraw;
                std::unordered_map<std::size_t, std::size_t> packet_by_node;
                packet_by_node.reserve(puppet.parts.size());
                for (std::size_t i = 0; i < puppet.parts.size(); ++i) {
                    packet_by_node.emplace(puppet.parts[i].node_index, i);
                }
                std::unordered_map<std::size_t, std::size_t> target_by_node;
                for (const auto& part : puppet.parts) {
                    std::size_t target = RealizedRenderable::kNoMaskTarget;
                    if (part.mask_source != PartDraw::kNoMaskSource) {
                        // A mask source that is not itself a drawn Part has no
                        // geometry to rasterise, so the Part stays unmasked
                        // rather than being clipped to nothing.
                        if (const auto pit2 =
                                packet_by_node.find(part.mask_source);
                            pit2 != packet_by_node.end())
                        {
                            const auto [it, inserted] = target_by_node.emplace(
                                part.mask_source,
                                prealized.puppet_mask_targets.size());
                            if (inserted) {
                                prealized.puppet_mask_targets.push_back(
                                    RealizedRenderable::PuppetMaskTarget{
                                        part.mask_source, pit2->second });
                            }
                            target = it->second;
                        }
                    }
                    prealized.puppet_part_mask_target.push_back(target);
                }
            }

            (void)pinserted;
            return &prealized;
        }

        // ── Star-field branch (#266) ────────────────────────────────────────
        // Mirror of the splat branch: no pull mesh; bind the resident star point
        // StructuredBuffer (asset-owned, found by the identity the star compiler
        // published under "star_catalog") at the StarCatalog semantic and record
        // a non-indexed vertex-id draw (vertex_count = 6 * star_count). The star
        // VS billboards a camera-facing quad per star; blend/depth are authored
        // on the program (additive, no depth write for a sky layer).
        if (source->star) {
            const StarFieldBinding& star = *source->star;
            if (star.star_count == 0u) {
                logger_.error("RhiSceneRenderer: star field has zero stars");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            const wz::rhi::GpuResourceHandle star_buffer =
                gpu_.resources.find(wz::rhi::ResourceIdentity{
                    ea::rhi_asset_identity(star.star_catalog_key, "star_catalog"),
                    {} });
            if (!star_buffer.valid()) {
                logger_.error(
                    "RhiSceneRenderer: star catalog buffer not resident");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }

            auto [tit, tinserted] =
                realized_renderables_.try_emplace(renderable_key);
            RealizedRenderable& trealized = tit->second;
            trealized.renderable_key = renderable_key;
            trealized.draw_layer = source->draw_layer;
            trealized.program = program->program;
            trealized.owns_buffers = false;  // buffer is asset-owned
            trealized.is_star_field = true;
            trealized.star_settings = star.settings;
            trealized.object_srg.reset(*slot2_layout);

            const wz::rhi::Tag star_semantic =
                gpu_.descriptor_semantics.find("star_catalog");
            const bool tsrg_ok =
                trealized.object_srg.set(star_semantic, star_buffer)
                    .has_value();
            if (!tsrg_ok || !trealized.object_srg.satisfies(*slot2_layout)) {
                realized_renderables_.erase(tit);
                logger_.error(
                    "RhiSceneRenderer: star object SRG build failed");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            if (!bind_view_constants(trealized, slot0_layout)) {
                realized_renderables_.erase(tit);
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }

            // 6 verts per star (two self-contained triangles), one non-indexed
            // draw of 6 * star_count. TriangleList program; the VS pulls each
            // star from the StarCatalog SRV by SV_VertexID.
            wz::rhi::GeometryView geometry;
            geometry.vertex_count = star.star_count * 6u;

            const StarFieldDrawConstants initial_star{};
            wz::rhi::DrawPacketAllocator allocator;
            wz::rhi::DrawPacketBuilder builder =
                wz::rhi::DrawPacketBuilder::begin(allocator);
            builder
                .set_geometry(geometry)
                .set_root_constants(std::span<const uint8_t>{
                    reinterpret_cast<const uint8_t*>(&initial_star),
                    sizeof(initial_star) })
                .add_shader_resource_group(trealized.object_srg);
            if (trealized.has_view_srg) {
                builder.add_shader_resource_group(trealized.view_srg);
            }
            if (!builder.add_draw_item(wz::rhi::DrawRequest{
                    forward_, trealized.program, nullptr,
                    wz::rhi::StreamBufferIndices{}, 0,
                    wz::rhi::DrawListMask::from(forward_) }))
            {
                realized_renderables_.erase(tit);
                logger_.error(
                    "RhiSceneRenderer: star draw packet build failed");
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            trealized.packet = builder.end();
            (void)tinserted;
            return &trealized;
        }

        // Prefer the asset-published GPU-resident pull buffers (gpu_sparse_mesh),
        // found by the identity the compiler used. Those are asset-owned, so the
        // renderer binds but never releases them. Fall back to a per-realize CPU
        // upload for sources without resident buffers (e.g. the rhi pull-mesh
        // recipe) — those the renderer owns and releases.
        // Resolve a normals pull buffer ONLY when the program's layout declares
        // the pulled_mesh_normals row (a lit program). Otherwise skip it — a
        // mesh's normals are irrelevant to a program that never binds them, and
        // uploading one would be a renderer-owned buffer nothing consumes.
        const wz::rhi::Tag pulled_normals_semantic =
            gpu_.descriptor_semantics.find("pulled_mesh_normals");
        const bool wants_normals = pulled_normals_semantic.valid()
            && wz::rhi::find_descriptor_binding_index(
                   *slot2_layout, pulled_normals_semantic);
        // Per-vertex UVs the same way (#290): only a program whose layout
        // declares the row gets them, so a mesh's texture coordinates cost
        // nothing for the many recipes that never sample a material.
        const wz::rhi::Tag pulled_uvs_semantic =
            gpu_.descriptor_semantics.find("pulled_mesh_uvs");
        const bool wants_uvs = pulled_uvs_semantic.valid()
            && wz::rhi::find_descriptor_binding_index(
                   *slot2_layout, pulled_uvs_semantic);

        wz::rhi::GpuResourceHandle positions_handle{};
        wz::rhi::GpuResourceHandle indices_handle{};
        wz::rhi::GpuResourceHandle normals_handle{};
        wz::rhi::GpuResourceHandle uvs_handle{};
        bool owns_buffers = false;
        // Which of the four buffers THIS realize created (vs found shared in
        // the registry). A failure below releases only the created ones.
        bool created_positions = false;
        bool created_indices = false;
        bool created_normals = false;
        bool created_uvs = false;
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;

        if (source->resident_key) {
            positions_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(*source->resident_key, "pull_positions"),
                {} });
            indices_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                ea::rhi_asset_identity(*source->resident_key, "pull_indices"),
                {} });
            if (wants_normals) {
                normals_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                    ea::rhi_asset_identity(*source->resident_key, "pull_normals"),
                    {} });
            }
            if (wants_uvs) {
                uvs_handle = gpu_.resources.find(wz::rhi::ResourceIdentity{
                    ea::rhi_asset_identity(*source->resident_key, "pull_uvs"),
                    {} });
            }
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
            // Pull buffers are deduped by mesh identity, so an acquire may
            // return a buffer another realized renderable is drawing with. A
            // FAILURE below may only release what THIS realize created —
            // releasing a found (shared) buffer destroys it under the sharer
            // (which has no self-heal at realize granularity). Probe the
            // registry before each acquire to learn created-vs-found.
            created_positions = !gpu_.resources.find(wz::rhi::ResourceIdentity{
                source->buffer_identity, position_variant }).valid();
            created_indices = !gpu_.resources.find(wz::rhi::ResourceIdentity{
                source->buffer_identity, index_variant }).valid();
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
                    gpu_.resources,
                    created_positions ? positions_handle
                                      : wz::rhi::GpuResourceHandle{},
                    created_indices ? indices_handle
                                    : wz::rhi::GpuResourceHandle{});
                failed_renderables_.insert(renderable_key);
                return nullptr;
            }
            owns_buffers = true;
            index_count = mesh->index_count();
            vertex_count = mesh->vertex_count();

            // Optional normals: a lit program pulls them; other recipes don't.
            // Best-effort — a failure here surfaces as an unsatisfied SRG below.
            if (wants_normals && mesh->has_normals) {
                const wz::rhi::Tag normal_variant =
                    ctx_.resource_variants.acquire("mesh.pull_normals");
                created_normals = !gpu_.resources.find(
                    wz::rhi::ResourceIdentity{
                        source->buffer_identity, normal_variant }).valid();
                const std::vector<float> normals = tight_mesh_normals(*mesh);
                normals_handle = acquire_pull_buffer(
                    gpu_.resources, source->buffer_identity, normal_variant,
                    normals.data(), normals.size() * sizeof(float),
                    3u * sizeof(float));
            }

            // Optional UVs, same shape (#290). The stride is float2, not the
            // float3 positions and normals share: a StructuredBuffer's stride
            // IS its element type, so a float3 stride here would walk the
            // shader off its own vertices.
            if (wants_uvs && mesh->has_uv0) {
                const wz::rhi::Tag uv_variant =
                    ctx_.resource_variants.acquire("mesh.pull_uvs");
                created_uvs = !gpu_.resources.find(
                    wz::rhi::ResourceIdentity{
                        source->buffer_identity, uv_variant }).valid();
                const std::vector<float> uvs = tight_mesh_uvs(*mesh);
                uvs_handle = acquire_pull_buffer(
                    gpu_.resources, source->buffer_identity, uv_variant,
                    uvs.data(), uvs.size() * sizeof(float),
                    2u * sizeof(float));
            }

            // Recover the lattice resolution from the mesh's level tags so the
            // per-frame view-transform packing can size the morph band. Only the
            // clipmap recipe rides this CPU-pull path with a lattice mesh.
            if (source->clipmap) {
                source->clipmap->base_resolution =
                    infer_clipmap_lattice_dims(*mesh).base_resolution;
            }
        }

        // A camera-snapped terrain recipe (source->clipmap) binds its height
        // texture generically via custom_bindings (scalar_field_texture) in the
        // custom SRG branch below — the same residency check every custom
        // binding gets — so there is no separate height-texture resolve here
        // (the 0x708 path that needed one was retired, #234).

        auto [it, inserted] = realized_renderables_.try_emplace(renderable_key);
        RealizedRenderable& realized = it->second;
        realized.renderable_key = renderable_key;
        realized.draw_layer = source->draw_layer;
        realized.program = program->program;
        realized.positions = positions_handle;
        realized.indices = indices_handle;
        realized.normals = normals_handle;
        realized.uvs = uvs_handle;
        realized.owns_buffers = owns_buffers;
        // Failure cleanup for the checks below: release only the pull buffers
        // THIS realize created — a found buffer is shared with a live
        // renderable and must survive this renderable's failure.
        const auto release_created_pull_buffers = [&]() {
            if (!realized.owns_buffers) {
                return;
            }
            release_unrealized_pull_buffers(
                gpu_.resources,
                created_positions ? realized.positions
                                  : wz::rhi::GpuResourceHandle{},
                created_indices ? realized.indices
                                : wz::rhi::GpuResourceHandle{},
                created_normals ? realized.normals
                                : wz::rhi::GpuResourceHandle{},
                created_uvs ? realized.uvs : wz::rhi::GpuResourceHandle{});
        };
        // Baked mesh-style shading (issue #195 slice A): mutually exclusive with
        // the clipmap pack branch below (a clipmap recipe never carries a style).
        if (source->style.has_style && !source->clipmap) {
            realized.has_style = true;
            realized.style = source->style;
        }
        if (source->custom) {
            // Custom recipe (issue #228): prebuild the root-constant block as
            // a byte template — zeroed, then the authored tail values written
            // at their compile-baked offsets. render_scene re-packs only the
            // head packer's dwords each frame; the tail persists.
            realized.is_custom = true;
            realized.custom_constants_head = source->custom_constants_head;
            realized.custom_constants.assign(
                static_cast<size_t>(source->custom_constants_dwords) * 4u,
                uint8_t{ 0 });
            for (const ea::RhiRenderableConstant& constant :
                 source->custom_constants)
            {
                const size_t offset =
                    static_cast<size_t>(constant.offset_dwords) * 4u;
                const size_t bytes =
                    static_cast<size_t>(constant.dwords) * 4u;
                if (offset + bytes > realized.custom_constants.size()) {
                    continue;  // compile-validated; defensive only
                }
                std::memcpy(
                    realized.custom_constants.data() + offset,
                    constant.value,
                    bytes);
            }
            // Keep the declared-field table (#229): per-instance scene-node
            // overrides address fields by name at pack time.
            realized.custom_fields = source->custom_constants;
        }
        // Camera-snapped terrain data (#233): a custom recipe with a height
        // texture carries the world settings + heightmap dims here, and the
        // lattice's level count / world width come from the mesh — the inputs
        // the shared terrain packer needs. Only 0x70A custom recipes reach this
        // now (0x708 retired, #234), so it feeds the custom head switch, never a
        // separate is_clipmap pack.
        if (source->clipmap) {
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
        const wz::rhi::Tag pulled_normals =
            gpu_.descriptor_semantics.find("pulled_mesh_normals");
        const wz::rhi::Tag pulled_uvs =
            gpu_.descriptor_semantics.find("pulled_mesh_uvs");
        bool srg_ok = true;
        if (source->custom) {
            // Generic bind (issue #228): the pull buffers go in only when the
            // program's authored layout declares the mesh-pull semantics, and
            // every other resource comes from the recipe's compiled bindings —
            // located by the identity its publisher used, no semantic table.
            // satisfies() below still proves the SRG complete against the
            // program's layout.
            if (wz::rhi::find_descriptor_binding_index(
                    *slot2_layout, pulled_positions))
            {
                srg_ok = realized.object_srg.set(
                    pulled_positions, realized.positions).has_value();
            }
            if (srg_ok
                && wz::rhi::find_descriptor_binding_index(
                    *slot2_layout, pulled_indices))
            {
                srg_ok = realized.object_srg.set(
                    pulled_indices, realized.indices).has_value();
            }
            // Normals bind the same way, but only when the layout declares them
            // (a lit program); other custom programs never see this.
            if (srg_ok
                && wz::rhi::find_descriptor_binding_index(
                    *slot2_layout, pulled_normals))
            {
                srg_ok = realized.object_srg.set(
                    pulled_normals, realized.normals).has_value();
            }
            // ...and UVs (#290), so a material texture can be sampled with the
            // mesh's own coordinates instead of one derived from its geometry.
            if (srg_ok
                && wz::rhi::find_descriptor_binding_index(
                    *slot2_layout, pulled_uvs))
            {
                srg_ok = realized.object_srg.set(
                    pulled_uvs, realized.uvs).has_value();
            }
            for (const ea::RhiRenderableBinding& binding :
                 source->custom_bindings)
            {
                if (!srg_ok) {
                    break;
                }
                const wz::rhi::Tag semantic =
                    gpu_.descriptor_semantics.find(binding.semantic);
                const wz::rhi::GpuResourceHandle resource =
                    gpu_.resources.find(wz::rhi::ResourceIdentity{
                        ea::rhi_asset_identity(binding.key, binding.variant),
                        {} });
                if (!semantic.valid() || !resource.valid()) {
                    logger_.error(
                        "RhiSceneRenderer: custom binding '"
                        + binding.semantic + "' is not resident");
                    srg_ok = false;
                    break;
                }
                srg_ok = realized.object_srg.set(semantic, resource)
                    .has_value();
            }
        }
        else {
            // Non-custom pull-mesh recipes bind only the two pull buffers. (The
            // 0x708 clipmap's height-texture bind lived here; retired by #234 —
            // a terrain look is now a custom recipe and takes the branch above.)
            srg_ok =
                realized.object_srg.set(pulled_positions, realized.positions)
                && realized.object_srg.set(pulled_indices, realized.indices);
        }
        if (!srg_ok || !realized.object_srg.satisfies(*slot2_layout)) {
            release_created_pull_buffers();
            realized_renderables_.erase(it);
            logger_.error("RhiSceneRenderer: object SRG build failed");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }
        if (!bind_view_constants(realized, slot0_layout)) {
            release_created_pull_buffers();
            realized_renderables_.erase(it);
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }

        wz::rhi::GeometryView geometry;
        geometry.index_buffer = realized.indices;
        geometry.index_count = index_count;
        geometry.vertex_count = vertex_count;

        // Initial root constants sized to the program's block: 64-byte identity
        // MVP for the pull path, the custom template (any size, incl. the
        // 128-byte terrain block) for a custom recipe, or the style block.
        // render_scene overwrites these every frame before recording, but
        // sizing them to the pipeline's root-constant dword_count up front keeps
        // the packet internally consistent (a size mismatch would make the
        // recorder reject).
        const wz::math::Mat4 initial_mvp = wz::math::Mat4::identity();
        const MeshStyleDrawConstants initial_style{};
        std::span<const uint8_t> initial_constants =
            std::span<const uint8_t>{
                reinterpret_cast<const uint8_t*>(initial_mvp.m),
                sizeof(initial_mvp.m) };
        if (realized.is_custom) {
            // The prebuilt template block — already the program's exact
            // root-constant size (possibly zero when the authored layout
            // declares no block; 128 bytes for the CameraSnappedTerrain head).
            initial_constants = std::span<const uint8_t>{
                realized.custom_constants.data(),
                realized.custom_constants.size() };
        }
        else if (realized.has_style) {
            initial_constants = std::span<const uint8_t>{
                reinterpret_cast<const uint8_t*>(&initial_style),
                sizeof(initial_style) };
        }
        wz::rhi::DrawPacketAllocator allocator;
        wz::rhi::DrawPacketBuilder builder =
            wz::rhi::DrawPacketBuilder::begin(allocator);
        builder.set_geometry(geometry);
        if (!initial_constants.empty()) {
            builder.set_root_constants(initial_constants);
        }
        builder.add_shader_resource_group(realized.object_srg);
        if (realized.has_view_srg) {
            builder.add_shader_resource_group(realized.view_srg);
        }
        if (!builder.add_draw_item(wz::rhi::DrawRequest{
                forward_, realized.program, nullptr,
                wz::rhi::StreamBufferIndices{}, 0,
                wz::rhi::DrawListMask::from(forward_) }))
        {
            release_created_pull_buffers();
            realized_renderables_.erase(it);
            logger_.error("RhiSceneRenderer: draw packet build failed");
            failed_renderables_.insert(renderable_key);
            return nullptr;
        }
        realized.packet = builder.end();

        (void)inserted;
        return &realized;
    }

    void RhiSceneRenderer::pack_camera_snapped_terrain_constants(
        const RealizedRenderable& realized,
        const wz::math::Mat4& node_world,
        const wz::math::Mat4& view_projection,
        const wz::math::Vec3& camera_world_pos,
        std::vector<uint8_t>& out)
    {
        // The lattice mesh is WORLD-SIZED (its compiler bakes the finest cell
        // metres into the vertex positions), so the renderer must NOT re-scale
        // it. Derive the placement from the MESH + the node TRANSLATION (not
        // node scale): c0 = mesh_width_x / grid_extent, the terrain world
        // footprint = c0 * heightmap_dims (world-absolute), node translation
        // places it (XZ → world origin, Y → base height). view_snapped carries
        // through from the recipe; node ROTATION is not applied.
        //
        // #218 Phase 2: when a Placement asset is connected, the recipe's baked
        // settings are authoritative for the texture→world footprint —
        // overriding the node-transform derivation so the visible terrain shares
        // one world mapping with a collision reading the same Placement. c0 stays
        // mesh-derived in ONE place (the placement never governs the lattice
        // geometry snap).
        const float world_translation[3] = {
            node_world.m[12], node_world.m[13], node_world.m[14] };
        const auto column_length = [&](int col) noexcept -> float {
            const int b = col * 4;
            return std::sqrt(
                node_world.m[b + 0] * node_world.m[b + 0]
                + node_world.m[b + 1] * node_world.m[b + 1]
                + node_world.m[b + 2] * node_world.m[b + 2]);
        };
        const float world_scale[3] = {
            column_length(0), column_length(1), column_length(2) };

        const bool placement_authoritative =
            realized.clipmap_settings.placement_authoritative;
        const ea::ClipmapLandscapeRenderSettings placement =
            wz::engine::rendering::compute_clipmap_placement(
                realized.clipmap_mesh_width_x,
                ea::ClipmapLatticeParams{
                    .level_count = realized.clipmap_level_count,
                    .base_resolution = realized.clipmap_base_resolution,
                    .cell_size = 1.0f,
                },
                realized.heightmap_width,
                realized.heightmap_height,
                world_translation,
                world_scale,
                realized.clipmap_settings.view_snapped,
                placement_authoritative ? &realized.clipmap_settings : nullptr);

        // One-shot diagnostic: where the footprint came from (a connected
        // Placement auto-aligns a collision reading the same Placement; a
        // node-transform footprint needs the node-scale hand-match).
        if (!clipmap_placement_logged_) {
            clipmap_placement_logged_ = true;
            logger_.info(
                std::string("clipmap placement: world_size=(")
                + std::to_string(placement.world_size[0]) + ", "
                + std::to_string(placement.world_size[1])
                + ") world_origin=("
                + std::to_string(placement.world_origin[0]) + ", "
                + std::to_string(placement.world_origin[1])
                + ") vertical_scale=" + std::to_string(placement.vertical_scale)
                + " | footprint source: "
                + (placement_authoritative
                    ? "Placement asset (auto-aligned with a collision reading "
                      "the same Placement)."
                    : "node transform (no Placement connected; to align a "
                      "collision constraint connect a shared Placement asset, "
                      "or set node scale X/Z = world_size)."));
        }

        const ClipmapDrawConstants constants = make_clipmap_draw_constants(
            view_projection,
            camera_world_pos,
            placement,
            realized.heightmap_width,
            realized.heightmap_height,
            realized.clipmap_base_resolution,
            realized.clipmap_level_count);
        const auto* bytes = reinterpret_cast<const uint8_t*>(&constants);
        out.assign(bytes, bytes + sizeof(constants));
    }

    bool RhiSceneRenderer::render_puppet_masks(
        RealizedRenderable& realized, uint32_t target_w, uint32_t target_h)
    {
        if (realized.puppet_mask_targets.empty()
            || target_w == 0u || target_h == 0u)
        {
            return true;
        }

        // (Re)build the mask targets when the render target's size changed. The
        // masked Part samples with its own target-space position, so a mask
        // rasterised for a different size would be read at the wrong scale.
        if (realized.puppet_mask_size[0] != target_w
            || realized.puppet_mask_size[1] != target_h)
        {
            const wz::rhi::Tag mask_semantic =
                gpu_.descriptor_semantics.find("puppet_mask");
            std::size_t index = 0;
            for (RealizedRenderable::PuppetMaskTarget& mask :
                 realized.puppet_mask_targets)
            {
                if (mask.resource.valid()) {
                    gpu_.resources.release(mask.resource);
                    mask.resource = {};
                    mask.render_target = {};
                }

                wz::rhi::GpuResourceDesc desc =
                    wz::rhi::GpuResourceDesc::texture_2d(
                        target_w, target_h,
                        wz::rhi::TextureFormat::RGBA8Unorm,
                        wz::rhi::ResourceUsage_Sampled
                            | wz::rhi::ResourceUsage_RenderTarget);
                // Identity is per renderable + source node, so two puppets (or
                // two sources) never collide in the registry.
                desc.identity = wz::rhi::ResourceIdentity{
                    ea::rhi_asset_identity(
                        realized.renderable_key,
                        "puppet_mask_" + std::to_string(mask.source_node)),
                    {} };

                mask.resource = gpu_.resources.acquire(desc);
                if (!mask.resource.valid()) {
                    logger_.error(
                        "RhiSceneRenderer: puppet mask target acquire failed");
                    realized.puppet_mask_size = { 0u, 0u };
                    return false;
                }
                const wz::rhi::GpuResource* res =
                    gpu_.resources.get(mask.resource);
                mask.render_target =
                    res ? gpu_.backend.gpu_handle_for(res->backend)
                        : wz::gpu::GPUHandle{};
                mask.identity = desc.identity;
                if (!mask.render_target.valid()) {
                    logger_.error(
                        "RhiSceneRenderer: puppet mask target has no GPU handle");
                    realized.puppet_mask_size = { 0u, 0u };
                    return false;
                }

                // Rebind every Part that samples this target. The packets hold
                // raw SRG pointers, so mutating the SRG in place is what makes
                // the already-built packets pick the new texture up.
                for (std::size_t pi = 0;
                     pi < realized.puppet_part_mask_target.size()
                     && pi < realized.puppet_part_srgs.size();
                     ++pi)
                {
                    if (realized.puppet_part_mask_target[pi] == index) {
                        (void)realized.puppet_part_srgs[pi].set(
                            mask_semantic, mask.resource);
                    }
                }
                ++index;
            }
            realized.puppet_mask_size = { target_w, target_h };
        }

        // Render each source alone into its target. One offscreen pass each --
        // passes do not nest, which is why this runs before the main pass opens.
        // The source draws with its OWN packet (and so its own blend variant):
        // only the alpha channel is read back as coverage, and every puppet
        // blend variant accumulates alpha the same "over" way, so the variant
        // does not change the mask.
        bool ok = true;
        for (const RealizedRenderable::PuppetMaskTarget& mask :
             realized.puppet_mask_targets)
        {
            if (!mask.render_target.valid()
                || mask.source_packet >= realized.puppet_packets.size())
            {
                continue;
            }
            const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            if (!wz::gpu::dx12::internal::begin_offscreen_pass(
                    gpu_.device, mask.render_target, clear))
            {
                logger_.error(
                    "RhiSceneRenderer: puppet mask pass begin failed");
                ok = false;
                continue;
            }
            wz::rhi::record_packet(
                realized.puppet_packets[mask.source_packet], forward_, recorder_);
            if (!recorder_.ready()) {
                logger_.error(
                    "RhiSceneRenderer: puppet mask draw rejected — "
                    + recorder_.last_reject_reason());
                ok = false;
            }
            wz::gpu::dx12::internal::end_offscreen_pass(
                gpu_.device, mask.render_target);
        }
        return ok;
    }

    void RhiSceneRenderer::update_puppet_pose(
        RealizedRenderable& realized, uint32_t target_w, uint32_t target_h)
    {
        namespace ino = wz::engine::assets::inochi;
        if (!realized.puppet_source) {
            return;
        }
        const ino::Puppet& puppet = *realized.puppet_source;
        const std::size_t node_count = puppet.nodes.size();
        if (node_count == 0) {
            return;
        }

        // Fit-to-target placement recomputed for THIS render's target dimensions
        // (#280), so the puppet lands whether the target is the backbuffer or an
        // arbitrary-sized offscreen texture -- not the realize-time viewport.
        const ino::Affine2D puppet_to_target = puppet_target_placement(
            realized.puppet_bounds_min, realized.puppet_bounds_max,
            static_cast<float>(target_w), static_cast<float>(target_h));

        // Time base + a subtle breathing bob (primary motion). SimplePhysics only
        // sways in response to a MOVING anchor -- a perfectly static puppet settles
        // to its gravity rest -- so a gentle vertical bob on the root feeds the
        // pendulums. The clock is animation_seconds_, advanced once per
        // simulation tick (#282); this function may run SEVERAL times per frame
        // (once per render target), and every run must read the same instant.
        const float t = static_cast<float>(animation_seconds_);
        constexpr float kTwoPi = 6.2831853f;

        // How much time this pose update owes the physics. Zero on the second
        // and later renders of a frame -- they rebuild the pose for their own
        // target but must not step the pendulums again. Also zero on the very
        // first render of a puppet, which has no elapsed time behind it.
        float physics_seconds = 0.0f;
        if (realized.puppet_clock_started) {
            const double elapsed =
                animation_seconds_ - realized.puppet_clock_seconds;
            if (elapsed > 0.0) {
                physics_seconds = static_cast<float>(elapsed);
            }
        }
        realized.puppet_clock_started = true;
        realized.puppet_clock_seconds = animation_seconds_;
        // A gentle vertical breathing bob on the root. This is the primary motion
        // the SimplePhysics pendulums react to -- a perfectly static puppet settles
        // to its gravity rest and stops -- and a visible sign of life on its own.
        // The amplitude scales with the puppet's height (~3.5%) so it reads on
        // puppets of any size (they can be thousands of px tall); a fixed px was
        // sub-percent and imperceptible here.
        constexpr float kBreathHz = 0.25f;
        const float bounds_h =
            realized.puppet_bounds_max[1] - realized.puppet_bounds_min[1];
        const float breath_amp = bounds_h > 1.0f ? bounds_h * 0.035f : 40.0f;
        const float breath = breath_amp * std::sin(t * kTwoPi * kBreathHz);

        // Primary deform = the breathing bob on the root (node 0), used to move the
        // pendulum anchors and folded into the rendered pose below.
        ino::PuppetDeform primary;
        primary.transform_deltas.assign(node_count, ino::TransformDelta{});
        primary.vertex_offsets.assign(node_count, {});
        primary.transform_deltas[0].trans[1] += breath;

        // Anchors = node world positions under the primary motion.
        const ino::PuppetNodeTransforms xf =
            ino::compute_node_world_transforms(puppet, &primary);
        std::vector<std::array<float, 2>> anchors(xf.world.size());
        for (std::size_t i = 0; i < xf.world.size(); ++i) {
            anchors[i] = { xf.world[i].tx, xf.world[i].ty };
        }

        // Physics writes the output parameters from the moving anchors. Stepped
        // at a FIXED 1/60 (what the integrator was tuned against), consuming the
        // elapsed time from an accumulator so the sway runs in real seconds on
        // any refresh rate (#282). The substep cap bounds the catch-up after a
        // hitch; leftover debt beyond it is dropped rather than spiralled.
        constexpr float kDt = 1.0f / 60.0f;
        constexpr int kMaxPhysicsSubsteps = 4;
        realized.puppet_physics_debt += physics_seconds;
        for (int step = 0; step < kMaxPhysicsSubsteps
                           && realized.puppet_physics_debt >= kDt; ++step) {
            ino::step_puppet_physics(
                puppet, realized.puppet_physics, anchors, kDt,
                realized.puppet_params);
            realized.puppet_physics_debt -= kDt;
        }
        if (realized.puppet_physics_debt > kDt) {
            realized.puppet_physics_debt = kDt;
        }

        // Physics-driven parameter deform, taken RELATIVE to the default-param
        // baseline (Inochi's base mesh IS the default pose), so default params map
        // to zero deform and only deviations move the puppet.
        ino::PuppetDeform pose =
            ino::evaluate_puppet_deform(puppet, realized.puppet_params);
        const ino::PuppetDeform& base = realized.puppet_deform_baseline;
        for (std::size_t i = 0; i < pose.transform_deltas.size()
                                && i < base.transform_deltas.size(); ++i) {
            ino::TransformDelta& d = pose.transform_deltas[i];
            const ino::TransformDelta& b = base.transform_deltas[i];
            for (int k = 0; k < 3; ++k) {
                d.trans[k] -= b.trans[k];
                d.rot[k] -= b.rot[k];
            }
            d.scale[0] -= b.scale[0];
            d.scale[1] -= b.scale[1];
            d.zsort -= b.zsort;
        }
        for (std::size_t i = 0; i < pose.vertex_offsets.size()
                                && i < base.vertex_offsets.size(); ++i) {
            std::vector<std::array<float, 2>>& vo = pose.vertex_offsets[i];
            const std::vector<std::array<float, 2>>& bo = base.vertex_offsets[i];
            const std::size_t n = vo.size() < bo.size() ? vo.size() : bo.size();
            for (std::size_t j = 0; j < n; ++j) {
                vo[j][0] -= bo[j][0];
                vo[j][1] -= bo[j][1];
            }
        }

        // Fold in the breathing root bob (on top of the relative deform).
        if (!pose.transform_deltas.empty()) {
            pose.transform_deltas[0].trans[1] += breath;
        }

        // Rebuild the deformed draw list (verts + placements), indexed by node.
        const ino::PuppetDrawList list = ino::build_puppet_draw_list(puppet, &pose);
        std::unordered_map<std::size_t, const ino::PuppetPartDraw*> by_node;
        by_node.reserve(list.parts.size());
        for (const ino::PuppetPartDraw& p : list.parts) {
            by_node.emplace(p.node_index, &p);
        }

        // Per Part: repack placement root constants (free) and refresh this Part's
        // slice of the puppet's SHARED vertex array. The array is uploaded ONCE
        // after the loop (#278) -- each GpuResourceRegistry::update is a
        // synchronous GPU flush, so the old per-Part buffers cost one stall per
        // moving Part. The per-Part change check survives because it decides
        // whether the single upload is needed at all.
        // (Manual min: <windows.h> min/max macros are in scope via the DX12 headers.)
        const std::size_t runtime_n = realized.puppet_part_runtime.size();
        const std::size_t packet_n = realized.puppet_packets.size();
        const std::size_t count = runtime_n < packet_n ? runtime_n : packet_n;
        bool any_moved = false;
        for (std::size_t pi = 0; pi < count; ++pi) {
            RealizedRenderable::PuppetPartRuntime& rt =
                realized.puppet_part_runtime[pi];
            const auto it = by_node.find(rt.node_index);
            if (it == by_node.end()) {
                continue;
            }
            const ino::PuppetPartDraw& dp = *it->second;

            // Placement root constants: recomputed every frame (CPU only, no GPU
            // sync), so breathing + transform-driven deform is always current.
            const ino::Affine2D m =
                ino::compose(puppet_to_target, dp.placement);
            const bool dp_masked =
                dp.mask_source != ino::PuppetPartDraw::kNoMaskSource
                && pi < realized.puppet_part_mask_target.size()
                && realized.puppet_part_mask_target[pi]
                       != RealizedRenderable::kNoMaskTarget;
            std::array<std::uint32_t, 24> block{};
            const float head[20] = {
                m.a, m.b, m.tx, dp.opacity,
                m.c, m.d, m.ty, 0.0f,
                dp.tint[0], dp.tint[1], dp.tint[2], 0.0f,
                dp.screen_tint[0], dp.screen_tint[1], dp.screen_tint[2], 0.0f,
                dp_masked ? dp.mask_threshold : 0.0f,
                (dp_masked && dp.mask_inverted) ? 1.0f : 0.0f,
                0.0f, 0.0f,
            };
            std::memcpy(block.data(), head, sizeof(head));
            block[20] = rt.vertex_base;
            block[21] = rt.index_base;
            realized.puppet_packets[pi].root_constants.assign(
                reinterpret_cast<const uint8_t*>(block.data()),
                reinterpret_cast<const uint8_t*>(block.data())
                    + block.size() * sizeof(std::uint32_t));

            // Vertex buffer: re-upload ONLY when the mesh moved past a sub-pixel
            // epsilon since the last upload. Each upload is a synchronous GPU flush
            // (#278), so skipping the many Parts that sit at the baseline (or move
            // imperceptibly) is what keeps the puppet from stalling the frame.
            // This Part's slice of the shared array, always kept current so the
            // single upload below carries every Part's latest geometry.
            if (!dp.vertices.empty()
                && rt.vertex_base + dp.vertices.size()
                       <= realized.puppet_vertex_scratch.size())
            {
                std::copy(
                    dp.vertices.begin(), dp.vertices.end(),
                    realized.puppet_vertex_scratch.begin() + rt.vertex_base);
            }

            // Does this Part's mesh actually differ from what was last uploaded?
            // Only that decides whether the one upload happens.
            const std::vector<std::array<float, 2>>* offs =
                rt.node_index < pose.vertex_offsets.size()
                    ? &pose.vertex_offsets[rt.node_index]
                    : nullptr;
            if (!offs || offs->empty() || dp.vertices.empty()) {
                continue;
            }
            constexpr float kOffsetEpsilon = 0.5f;  // px in puppet space
            bool changed = !rt.ever_uploaded
                || rt.last_offsets.size() != offs->size();
            for (std::size_t vi = 0; !changed && vi < offs->size(); ++vi) {
                if (std::abs((*offs)[vi][0] - rt.last_offsets[vi][0])
                        > kOffsetEpsilon
                    || std::abs((*offs)[vi][1] - rt.last_offsets[vi][1])
                        > kOffsetEpsilon)
                {
                    changed = true;
                }
            }
            if (changed) {
                rt.last_offsets = *offs;
                rt.ever_uploaded = true;
                any_moved = true;
            }
        }

        // ONE upload for the whole puppet, and only when something moved. This
        // is the #278 payoff: a deforming Aka went from one synchronous GPU
        // flush per moving Part to exactly one per frame.
        if (any_moved && realized.puppet_vertices.valid()
            && !realized.puppet_vertex_scratch.empty())
        {
            gpu_.resources.update(
                realized.puppet_vertices,
                realized.puppet_vertex_scratch.data(),
                static_cast<std::uint64_t>(
                    realized.puppet_vertex_scratch.size())
                    * sizeof(ino::PuppetVertex));
        }
    }

    bool RhiSceneRenderer::render_scene(
        std::span<const ea::SceneNodeAsset> nodes,
        ea::EngineAssetLibrary& assets,
        const wz::math::Mat4& view_projection,
        const wz::math::Vec3& camera_world_pos,
        std::span<const wz::math::Mat4> world_transforms,
        const ea::AtmosphereData* atmosphere,
        wz::gpu::GPUHandle offscreen_target)
    {
        ID3D12GraphicsCommandList* cmd =
            wz::gpu::dx12::internal::get_command_list(gpu_.device);
        if (!cmd) {
            return false;
        }

        // Offscreen render-to-texture (S6): render into a render-target texture at
        // its own dimensions instead of the backbuffer. Resolve + validate it up
        // front; the target size then drives the screen block + viewport.
        const bool to_offscreen = offscreen_target.valid();
        uint32_t target_w = wz::gpu::dx12::internal::get_width(gpu_.device);
        uint32_t target_h = wz::gpu::dx12::internal::get_height(gpu_.device);
        if (to_offscreen) {
            const wz::gpu::dx12::internal::DX12Texture* rt =
                wz::gpu::dx12::internal::get_dx12_texture(
                    gpu_.device, offscreen_target);
            if (!rt || !rt->valid() || !rt->is_render_target) {
                logger_.error(
                    "RhiSceneRenderer: render_scene offscreen_target is not a "
                    "render-target texture");
                return false;
            }
            target_w = rt->width;
            target_h = rt->height;
        }

        // The frame's VIEW-frequency state: one pack, one upload, read by every
        // program that declared a view block. Packed unconditionally (it is
        // three float4s) so a mid-frame realize can seed a freshly acquired
        // buffer with the same values; the UPLOAD is skipped while no program
        // has asked for the buffer, which is every frame today.
        view_constants_ = make_view_constants(camera_world_pos, atmosphere);
        if (view_constants_buffer_.valid()) {
            // Refresh in place: the handle and identity are stable, so the
            // recorder's cached descriptor tables keep viewing the same
            // resource and only the bytes change (the #145 per-frame door).
            if (!gpu_.resources.update(
                    view_constants_buffer_, &view_constants_,
                    sizeof(view_constants_)))
            {
                logger_.error(
                    "RhiSceneRenderer: view constants refresh failed");
            }
        }

        // The screen block's per-frame refresh, from the render target's size.
        // Same #145 door as the view block; only bound where a layout declares
        // the Screen head (2D overlays), otherwise acquired-but-idle.
        screen_constants_ = make_screen_constants(
            static_cast<float>(target_w),
            static_cast<float>(target_h));
        if (screen_constants_buffer_.valid()) {
            if (!gpu_.resources.update(
                    screen_constants_buffer_, &screen_constants_,
                    sizeof(screen_constants_)))
            {
                logger_.error(
                    "RhiSceneRenderer: screen constants refresh failed");
            }
        }

        // Per-frame precise reclamation: drain any pending release whose last
        // touch the GPU has now passed, then tag this frame's timeline onto the
        // recorder so every resource it resolves is kept resident until this
        // frame completes. Cheap no-op when nothing is pending.
        gpu_.resources.collect(
            wz::gpu::completed_timeline_value(gpu_.device));
        recorder_.set_frame_timeline(
            wz::gpu::frame_timeline_value(gpu_.device));

        uint32_t recorded = 0;

        // Hierarchical world transforms: each node's local TRS composed with its
        // parent chain, so a renderable child follows its parent. The RHI path
        // had drawn every node at its own local transform; this restores
        // inheritance without resurrecting the legacy compile_scene renderer.
        // The clipmap branch ignores this by design (its lattice follows the
        // camera, not the node).
        //
        // #221: the caller may pass the per-node world matrices (drawn from the
        // live simulation polytree, the single source of truth). Use them when
        // they line up with `nodes`; otherwise (empty span, or size mismatch)
        // compose from the authored nodes here, exactly as before.
        std::vector<wz::math::Mat4> composed_node_worlds;
        if (world_transforms.size() != nodes.size()) {
            composed_node_worlds = compute_scene_node_world_transforms(nodes);
        }
        const std::span<const wz::math::Mat4> node_worlds =
            world_transforms.size() == nodes.size()
                ? world_transforms
                : std::span<const wz::math::Mat4>(composed_node_worlds);
        // Inherited visibility: a hidden parent hides its whole subtree (e.g. a
        // scene-source host hiding its grafted children), not just itself.
        const std::vector<std::uint8_t> node_effective_visible =
            compute_scene_node_effective_visibility(nodes);

        // Draw order: world geometry first, the overlay layer last (on top). The
        // renderer records into the command list in this order, and overlays are
        // depth-disabled + alpha-blended, so recording them last is what puts them
        // over the scene -- an overlay has no depth to sort by. A STABLE partition:
        // node order is preserved within a layer, so authored ordering still
        // breaks ties. Realizing here is free -- ensure_renderable is cached, so
        // the main loop below finds each renderable already built.
        std::vector<std::size_t> draw_order;
        draw_order.reserve(nodes.size());
        for (int layer = 0; layer < ea::kDrawLayerCount; ++layer) {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                if (!node_effective_visible[i] || !nodes[i].renderable_asset) {
                    continue;
                }
                const RealizedRenderable* r =
                    ensure_renderable(assets, *nodes[i].renderable_asset);
                if (r != nullptr
                    && static_cast<int>(r->draw_layer) == layer)
                {
                    draw_order.push_back(i);
                }
            }
        }

        // ── Puppet prepass (S3c/S7 pose + #275 masks) ───────────────────────
        // Both must happen BEFORE the frame's pass opens. The pose repack feeds
        // the mask draws (a mask is the source Part rasterised through the same
        // placement), and each mask is its OWN offscreen pass -- passes do not
        // nest. update_puppet_pose also re-uploads moved vertex buffers, and
        // those uploads are synchronous GPU flushes, which is another thing not
        // to do with a render pass open.
        for (const std::size_t node_index : draw_order) {
            if (!nodes[node_index].renderable_asset) {
                continue;
            }
            RealizedRenderable* realized =
                ensure_renderable(assets, *nodes[node_index].renderable_asset);
            if (!realized || !realized->is_puppet) {
                continue;
            }
            update_puppet_pose(*realized, target_w, target_h);
            (void)render_puppet_masks(*realized, target_w, target_h);
        }

        // The frame's pass. Opened here, after every prepass has closed its own.
        if (to_offscreen) {
            // Bind the render-target texture (transitions it to RENDER_TARGET, sets
            // its viewport, and clears it to transparent). Draws below land in it.
            const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            if (!wz::gpu::dx12::internal::begin_offscreen_pass(
                    gpu_.device, offscreen_target, clear)) {
                logger_.error("RhiSceneRenderer: begin_offscreen_pass failed");
                return false;
            }
        }
        else {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv =
                wz::gpu::dx12::internal::get_current_rtv(gpu_.device);
            D3D12_CPU_DESCRIPTOR_HANDLE dsv =
                wz::gpu::dx12::internal::get_dsv(gpu_.device);
            cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

            const float w = static_cast<float>(target_w);
            const float h = static_cast<float>(target_h);
            D3D12_VIEWPORT viewport{ 0.0f, 0.0f, w, h, 0.0f, 1.0f };
            cmd->RSSetViewports(1, &viewport);
            D3D12_RECT scissor{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
            cmd->RSSetScissorRects(1, &scissor);
        }

        // Renderables whose packets were rejected for a STALE resource this
        // frame. Their realized entries are erased AFTER the pass closes (a
        // re-realize does synchronous GPU uploads, which must not happen with
        // a pass open), so the next frame rebuilds them — one dark frame, then
        // healed — instead of erroring every frame until a graph swap.
        std::vector<wz::asset::AssetKey> stale_rejected;

        for (const std::size_t node_index : draw_order) {
            const ea::SceneNodeAsset& node = nodes[node_index];
            if (!node_effective_visible[node_index] || !node.renderable_asset) {
                continue;
            }
            RealizedRenderable* realized =
                ensure_renderable(assets, *node.renderable_asset);
            if (!realized) {
                continue;
            }

            if (realized->is_splat_cloud) {
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
            else if (realized->is_star_field) {
                // world + view_proj + camera pos + (star_size, intensity). The
                // star VS rotates each star direction by `world` (so the scene
                // node orients the celestial sphere), places it on the far
                // sphere relative to the camera, sizes the billboard by
                // star_size, and scales its radiance by intensity -- both live
                // authored dials, not shader constants.
                const wz::math::Mat4& world =
                    node_worlds[static_cast<std::size_t>(&node - nodes.data())];
                // The animation clock, advanced once per simulation tick by the
                // real frame delta (#282) -- so twinkle runs at the same rate
                // whatever the refresh rate is, and a second (offscreen) render
                // of this frame sees the same instant rather than advancing it.
                // A scene that is only rendered, never ticked (tests), sees 0.
                const float time_seconds =
                    static_cast<float>(animation_seconds_);
                const StarFieldDrawConstants constants =
                    make_star_field_draw_constants(
                        world, view_projection, camera_world_pos,
                        realized->star_settings.star_size,
                        realized->star_settings.intensity,
                        time_seconds,
                        realized->star_settings.twinkle_amount,
                        realized->star_settings.twinkle_speed);
                const auto* bytes =
                    reinterpret_cast<const uint8_t*>(&constants);
                realized->packet.root_constants.assign(
                    bytes, bytes + sizeof(constants));
            }
            else if (realized->is_custom) {
                // Custom recipe (issue #228): re-pack only the head packer's
                // dwords in the prebuilt template — the authored tail values
                // baked at realize time persist behind it — then hand the
                // whole block to the packet. None leaves the block fully
                // authored/static.
                const wz::math::Mat4& world = node_worlds[node_index];
                std::vector<uint8_t>& block = realized->custom_constants;
                switch (realized->custom_constants_head) {
                case ea::RenderBindingConstantsHead::Mvp16: {
                    const wz::math::Mat4 mvp =
                        wz::math::mul(view_projection, world);
                    if (block.size() >= sizeof(mvp.m)) {
                        std::memcpy(block.data(), mvp.m, sizeof(mvp.m));
                    }
                    break;
                }
                case ea::RenderBindingConstantsHead::WorldViewProjCamera36: {
                    // The splat-cloud packer IS this head's contract; the
                    // trailing "diameter" float is splat-recipe data a custom
                    // renderable does not carry — packed as 0.
                    const SplatCloudDrawConstants head =
                        make_splat_cloud_draw_constants(
                            world, view_projection, camera_world_pos, 0.0f);
                    if (block.size() >= sizeof(head)) {
                        std::memcpy(block.data(), &head, sizeof(head));
                    }
                    break;
                }
                case ea::RenderBindingConstantsHead::CameraSnappedTerrain:
                    // Camera-follow terrain (issue #233): the head IS the whole
                    // 32-dword block (no authored tail — the layout declares no
                    // tail fields with this head), packed fresh each frame by
                    // the SAME shared packer the 0x708 clipmap uses. realize
                    // populated the clipmap settings/dims (source->clipmap).
                    pack_camera_snapped_terrain_constants(
                        *realized, world, view_projection, camera_world_pos,
                        block);
                    break;
                case ea::RenderBindingConstantsHead::None:
                    break;
                }
                realized->packet.root_constants.assign(
                    block.begin(), block.end());
                // Per-instance constant overrides (#229) land in the PACKET's
                // copy, after the template assign: the shared template keeps
                // only graph-authored defaults, so nodes sharing this recipe
                // don't leak override values into each other.
                if (!node.renderable_constants.empty()) {
                    merge_renderable_constant_overrides(
                        realized->packet.root_constants,
                        realized->custom_fields,
                        node.renderable_constants);
                }
            }
            else if (realized->has_style) {
                // Styled pull mesh (issue #195 slice A): MVP + baked shading in
                // one 28-dword block. Same MVP the plain path computes, followed
                // by the recipe's style colours/alpha so the PS can shade.
                const wz::math::Mat4& world =
                    node_worlds[static_cast<std::size_t>(&node - nodes.data())];
                const wz::math::Mat4 mvp = wz::math::mul(view_projection, world);
                const MeshStyleDrawConstants constants =
                    make_mesh_style_draw_constants(mvp, realized->style);
                const auto* bytes =
                    reinterpret_cast<const uint8_t*>(&constants);
                realized->packet.root_constants.assign(
                    bytes, bytes + sizeof(constants));
            }
            else if (realized->is_puppet) {
                // Pose + masks already ran in the prepass above (they cannot run
                // with a pass open); nothing to repack here.
            }
            else {
                const wz::math::Mat4& world =
                    node_worlds[static_cast<std::size_t>(&node - nodes.data())];
                const wz::math::Mat4 mvp = wz::math::mul(view_projection, world);
                realized->packet.root_constants.assign(
                    reinterpret_cast<const uint8_t*>(mvp.m),
                    reinterpret_cast<const uint8_t*>(mvp.m) + sizeof(mvp.m));
            }

            if (realized->is_puppet) {
                uint32_t part_index = 0;
                for (const wz::rhi::DrawPacket& pkt : realized->puppet_packets) {
                    wz::rhi::record_packet(pkt, forward_, recorder_);
                    ++recorded;
                    if (!recorder_.ready()) {
                        logger_.error(
                            "RhiSceneRenderer: puppet part "
                            + std::to_string(part_index) + " rejected — "
                            + recorder_.last_reject_reason());
                        if (recorder_.last_reject_was_stale_resource()) {
                            stale_rejected.push_back(*node.renderable_asset);
                        }
                    }
                    ++part_index;
                }
            }
            else {
                wz::rhi::record_packet(realized->packet, forward_, recorder_);
                ++recorded;
                if (!recorder_.ready()) {
                    logger_.error(
                        "RhiSceneRenderer: renderable packet rejected — "
                        + recorder_.last_reject_reason());
                    if (recorder_.last_reject_was_stale_resource()) {
                        stale_rejected.push_back(*node.renderable_asset);
                    }
                }
            }
        }

        // Offscreen render-to-texture (S6): close the pass so the target lands in a
        // sampleable state and the backbuffer is rebound for the rest of the frame.
        if (to_offscreen) {
            wz::gpu::dx12::internal::end_offscreen_pass(
                gpu_.device, offscreen_target);
        }

        // Self-heal stale-resource rejections: drop those realized entries now
        // that the pass is closed, so the next frame's pre-pass re-realizes
        // them (a fresh acquire rebuilds the destroyed buffer). A cause that
        // persists fails the re-realize and lands in failed_renderables_, so
        // this cannot ping-pong realize/reject forever.
        for (const wz::asset::AssetKey& key : stale_rejected) {
            realized_renderables_.erase(key);
        }

        if (recorded == 0) {
            return true;
        }
        if (!recorder_.ready()) {
            logger_.error(
                "RhiSceneRenderer: recorder rejected a draw — "
                + recorder_.last_reject_reason());
            return false;
        }
        return true;
    }
}
