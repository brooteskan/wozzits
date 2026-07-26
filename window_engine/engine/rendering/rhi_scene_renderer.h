#pragma once

// engine/rendering/rhi_scene_renderer.h
//
// RhiSceneRenderer — the reusable RHI scene render path (the "RPI tier"). It owns
// the RhiContext, pipeline cache, command recorder, and the realized-program /
// realized-renderable caches, and renders a scene's renderables (resolved from
// the asset system) into RHI draw packets via wozzits-rhi (DrawPacket /
// record_packet). Apps own the window + device and drive it via render_scene().
//
// The realize path is lifted from the scene editor's working RHI render path so
// the app and editor share one implementation (no legacy dx12 submit, no
// duplication).

#include <engine/rendering/rhi_context.h>
#include <engine/rendering/rhi_dx12_command_recorder.h>
#include <engine/rendering/rhi_dx12_pipeline.h>
#include <engine/rendering/engine_gpu_context.h>
#include <engine/rendering/rhi_gpu_backend.h>

// For ClipmapLandscapeRenderSettings carried on a realized clipmap renderable.
#include <engine/assets/renderable/renderable.h>
#include <engine/assets/inochi/puppet_deform.h>
#include <engine/assets/inochi/puppet_physics.h>
#include <engine/assets/inochi/puppet_draw_list.h>

#include <gpu/gpu.h>
#include <logging/logger.h>
#include <math/math_types.h>

#include <asset/types.h>

#include <wozzits/rhi/draw_list_tag.h>
#include <wozzits/rhi/draw_packet.h>
#include <wozzits/rhi/frame_graph.h>
#include <wozzits/rhi/gpu_resource.h>
#include <wozzits/rhi/shader_resource_group.h>

#include <cstddef>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wz::engine::assets
{
    class EngineAssetLibrary;
    struct AtmosphereData;
    struct SceneNodeAsset;
    struct SceneRenderableConstantOverride;
}

namespace wz::engine::rendering
{
    // ─── ViewConstants ──────────────────────────────────────────────────────
    //
    // The VIEW-frequency block: the observer, and the atmosphere it looks
    // through. Filled ONCE per frame and read by every program in that frame,
    // at a frequency no renderable owns — which is the whole point of the
    // space-0 group (a8d3450). Fog is its first tenant; scene lighting is the
    // obvious next one.
    //
    // Packed BYTE-FOR-BYTE to match the WzViewConstants struct the binding
    // prelude emits for RenderBindingViewHead::CameraFog (render_binding_
    // layout.cpp), which a shader reads as view_constants[0] off a one-element
    // StructuredBuffer at register(t0, space0). A buffer rather than a cbuffer
    // because the DX12 pipeline realizes exactly ONE root-constant block per
    // program and the object block already claims it (3e25b63, wozzits-rhi#7).
    //
    //   offset  field
    //   ------  ------------------------------------------------------------
    //      0     camera       (xyz = camera world position, w unused)
    //     16     fog_color    (rgb = colour, w = density)
    //     32     fog_params   (x = fog start distance, y = height falloff,
    //                           z = enabled (1 or 0), w unused)
    //   ------
    //    48 bytes = 12 dwords.
    //
    // Declared here rather than beside the per-draw constant structs in
    // rhi_scene_renderer.cpp because the packing is unit-tested: the ONLY
    // thing holding this and the generated HLSL together is that the two
    // agree on these offsets, and nothing else checks it.
    struct ViewConstants
    {
        float camera[4];
        float fog_color[4];
        float fog_params[4];
    };
    static_assert(sizeof(ViewConstants) == 48,
        "view constants must be 48 bytes (12 dwords) to match the "
        "RenderBindingViewHead::CameraFog SRG row and the HLSL WzViewConstants "
        "struct the binding prelude emits");

    // ─── ScreenConstants ────────────────────────────────────────────────────
    //
    // The VIEW-frequency block for RenderBindingViewHead::Screen: the viewport
    // size, so a screen-space pass (2D overlays / HUD) can map a pixel rect to
    // NDC. Packed byte-for-byte to match the WzScreenConstants struct the binding
    // prelude emits (render_binding_layout.cpp), read as screen_constants[0] off a
    // one-element StructuredBuffer at t0 of the view register space.
    struct ScreenConstants
    {
        float viewport[4];   // xy = width, height; zw = 1/width, 1/height
    };
    static_assert(sizeof(ScreenConstants) == 16,
        "screen constants must be 16 bytes (4 dwords) to match the "
        "RenderBindingViewHead::Screen SRG row and the HLSL WzScreenConstants "
        "struct the binding prelude emits");

    // Build the frame's screen constants from the render target's dimensions.
    // A zero dimension yields a zero inverse rather than a divide-by-zero.
    [[nodiscard]] ScreenConstants make_screen_constants(
        float viewport_width, float viewport_height) noexcept;

    // Build the frame's view constants. The camera goes in REGARDLESS of the
    // atmosphere — it is view state, not fog state, and hoisting the observer
    // to its own frequency is what makes a shared fog helper possible at all.
    // A null atmosphere means "no fog": zeroed fog terms with fog_params.z
    // (enabled) 0, which is exactly the value wz_fog_from_view tests before it
    // does anything, so a frame with no authored atmosphere returns the shaded
    // colour untouched.
    [[nodiscard]] ViewConstants make_view_constants(
        const wz::math::Vec3& camera_world_pos,
        const wz::engine::assets::AtmosphereData* atmosphere) noexcept;

    class RhiSceneRenderer
    {
    public:
        RhiSceneRenderer(EngineGpuContext& gpu, wz::Logger& logger);

        RhiSceneRenderer(const RhiSceneRenderer&)            = delete;
        RhiSceneRenderer& operator=(const RhiSceneRenderer&) = delete;
        RhiSceneRenderer(RhiSceneRenderer&&)                 = delete;
        RhiSceneRenderer& operator=(RhiSceneRenderer&&)      = delete;

        // Advance the renderer's per-frame state by the real frame delta. Called
        // once per simulation tick, BEFORE any render_scene for that frame; the
        // render path only reads the clock this advances (#282).
        void simulation_tick(float dt_seconds);

        // The animation clock this renderer draws against, in seconds (#282).
        // Read by the refresh gates (#288): content driven by the clock rather
        // than by its inputs -- a puppet's deform, a star field's twinkle --
        // stops changing exactly when this stops advancing.
        [[nodiscard]] double animation_seconds() const noexcept
        {
            return animation_seconds_;
        }

        // Render the scene's renderables. The caller owns the device frame
        // boundaries (begin/clear/end/present); this binds the current backbuffer
        // targets and records draw packets for every visible node that carries a
        // resolved renderable_asset key. Returns false if the recorder rejected a
        // draw. Renderables/programs are resolved from `assets` and cached.
        //
        // camera_world_pos is the camera's world-space position. Standard pull /
        // gpu_sparse renderables ignore it; a clipmap-landscape renderable uses
        // its XZ to center + snap the lattice and pack the per-draw clipmap view
        // transform. Pass {0,0,0} when there is no meaningful camera.
        //
        // world_transforms, when non-empty, is the per-node world matrix the
        // renderer draws with (index-aligned with `nodes`). The caller supplies
        // it so the drawn pose can come from the live simulation polytree (the
        // single source of truth) rather than the authored composition. When it
        // is empty (or its size doesn't match `nodes`), the renderer falls back
        // to composing the transforms itself via
        // compute_scene_node_world_transforms(nodes) — the pre-#221 behavior the
        // renderer-unit tests rely on.
        //
        // atmosphere is the frame's global fog (the resolved Atmosphere asset,
        // 6c51cf5). It reaches the shaders through the VIEW-frequency group at
        // register space 0, never through a per-draw constant, because fog
        // belongs to the world and the observer rather than to any one thing
        // being drawn — every program must see the same values or the scene
        // disagrees with itself. nullptr means "no fog"; the camera is packed
        // into the view constants either way.
        bool render_scene(
            std::span<const wz::engine::assets::SceneNodeAsset> nodes,
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos,
            std::span<const wz::math::Mat4> world_transforms = {},
            const wz::engine::assets::AtmosphereData* atmosphere = nullptr,
            // Offscreen render-to-texture (S6): when valid, the scene renders into
            // this render-target texture (at its own dimensions), leaving it
            // sampleable, instead of the backbuffer. The texture must have been
            // created with ResourceUsage_RenderTarget. Default = the backbuffer.
            wz::gpu::GPUHandle offscreen_target = {});

        // Invalidate every realized cache after a wholesale asset-graph swap.
        // The caches (realized programs/renderables/registered shaders) are
        // keyed by the OUTGOING graph's AssetKeys, so on a swap they go stale —
        // the renderer would keep drawing the previous graph's GPU resources.
        // This deferred-releases the outgoing graph's pull buffers and clears
        // the caches
        // so the next render re-realizes against the new keys. A graph swap is a
        // rare, heavy editor action (replace-the-draft), so the flush this does
        // to make the release safe is acceptable here; it is NOT a per-frame path.
        void on_graph_changed();

        // GPU resources currently resident in the resource registry. Flat across
        // a graph swap once the new graph is realized (the outgoing graph's were
        // released): diagnostics + the rebind regression test read this.
        [[nodiscard]] std::size_t resident_gpu_resource_count() const
        {
            return gpu_.resources.resident_count();
        }

        // Render programs / shader modules currently registered in the rhi
        // context. Both drop to 0 on a graph swap (on_graph_changed clears them)
        // so the fixed-capacity registries don't grow across binds — the rebind
        // test asserts this.
        [[nodiscard]] std::size_t registered_program_count() const
        {
            return gpu_.programs.size();
        }
        [[nodiscard]] std::size_t registered_shader_count() const
        {
            return gpu_.shaders.size();
        }

        // Programs the renderer bridged at render time (the find-then-fallback
        // fallback path) instead of binding one the asset compiler produced. The
        // rebind test asserts this is 0 after the first render of the migrated
        // custom program — proving it came from the compiler, the way resident ==
        // 2 proved the resident-buffer path in #190. Cumulative since construction.
        [[nodiscard]] std::size_t render_time_program_bridge_count() const
        {
            return render_time_program_bridges_;
        }

        // SRV descriptor tables cached by the command recorder. Resets to 0 on a
        // graph swap (on_graph_changed releases them) so descriptor-heap ranges
        // don't leak across binds.
        [[nodiscard]] std::size_t cached_descriptor_table_count() const
        {
            return recorder_.cached_descriptor_table_count();
        }

    private:
        struct RealizedProgram
        {
            wz::asset::AssetKey program_key{};
            wz::asset::AssetKey vertex_shader_key{};
            wz::asset::AssetKey pixel_shader_key{};
            wz::rhi::Tag        program{};
        };
        struct RealizedRenderable
        {
            wz::asset::AssetKey         renderable_key{};
            // Compositing order (copied from the recipe at realize): Overlay
            // draws last / on top, so the submit loop records it after world
            // geometry.
            wz::engine::assets::DrawLayer draw_layer =
                wz::engine::assets::DrawLayer::World;
            wz::rhi::Tag                program{};
            wz::rhi::GpuResourceHandle  positions{};
            wz::rhi::GpuResourceHandle  indices{};
            wz::rhi::GpuResourceHandle  normals{};   // empty unless a lit layout pulls them
            wz::rhi::ShaderResourceGroup object_srg{};
            // The VIEW-frequency group (register space 0), pointing at the
            // renderer's single view-constants buffer. Built ONLY when this
            // renderable's realized program has a slot-0 layout that declares
            // the view_constants row — i.e. when its binding layout opted into
            // a view head. has_view_srg gates whether the draw packet carries
            // it; false is the normal case (nothing authored declares a view
            // head), and the packet is then byte-identical to what it was
            // before this existed.
            //
            // Per-renderable rather than one shared group because the SRG is
            // built against a SPECIFIC layout and validates against its hash;
            // two programs may declare different slot-0 layouts. They all bind
            // the same buffer. Its address must be stable — DrawPacket stores
            // SRG POINTERS — which holds because RealizedRenderable lives in a
            // node-based unordered_map, exactly as object_srg relies on.
            wz::rhi::ShaderResourceGroup view_srg{};
            bool                        has_view_srg = false;
            wz::rhi::DrawPacket         packet{};
            // True only for the CPU-upload fallback path, where the renderer
            // acquired these pull buffers and must release them. Resident
            // (asset-published) buffers are owned by the asset library; the
            // renderer binds but never releases them.
            bool                        owns_buffers = false;

            // Camera-snapped terrain (#233): a custom recipe (0x70A) with the
            // CameraSnappedTerrain head displaces the lattice in the VS by
            // sampling a resident height texture and packs a per-frame view
            // transform (pack_camera_snapped_terrain_constants) via the custom
            // head switch. These settings + heightmap dims feed compute_clipmap_
            // view; they're populated when source->clipmap is present (the
            // height texture binds generically at the scalar_field_texture
            // semantic). Empty for a non-terrain look.
            wz::engine::assets::ClipmapLandscapeRenderSettings clipmap_settings{};
            uint32_t heightmap_width = 1;
            uint32_t heightmap_height = 1;
            // Lattice base_resolution + level_count recovered from the mesh level
            // tags. base_resolution sizes the per-level geomorph band (#207);
            // together they give the lattice's unitless grid extent
            // (clipmap_lattice_grid_extent), which divides the mesh's world width
            // to recover the finest cell world size c0 (the lattice mesh is now
            // WORLD-SIZED — its positions bake cell_size in — so c0 comes from the
            // mesh, not the node scale).
            uint32_t clipmap_base_resolution = 8;
            uint32_t clipmap_level_count = 1;
            // World-space width of the lattice mesh along X (max.x - min.x),
            // captured at realize time. c0 = clipmap_mesh_width_x /
            // clipmap_lattice_grid_extent({level_count, base_resolution}); see
            // compute_clipmap_placement.
            float clipmap_mesh_width_x = 0.0f;

            // Gaussian-splat-cloud renderables (#208) bind the resident decoded
            // splat StructuredBuffer (at the SplatCloud semantic in object_srg,
            // asset-owned, never released) and record a non-indexed
            // DrawInstanced of 4 * splat_count vertices. is_splat_cloud gates the
            // per-frame SplatCloudDrawConstants packing in render_scene.
            bool is_splat_cloud = false;
            wz::engine::assets::GaussianSplatCloudRenderSettings splat_settings{};

            // Inochi2D puppet renderables (inochi S2b): the FIRST N-packet
            // renderable. A puppet records one screen-space DrawPacket per drawable
            // Part in the Overlay layer, back-to-front by zsort. is_puppet gates the
            // record fan-out; puppet_packets holds the per-Part packets and
            // puppet_part_srgs their object SRGs -- reserved once at realize so
            // their addresses stay stable (the packets hold raw SRG pointers, the
            // same invariant object_srg relies on).
            bool is_puppet = false;
            std::vector<wz::rhi::DrawPacket> puppet_packets;
            std::vector<wz::rhi::ShaderResourceGroup> puppet_part_srgs;

            // Deform + physics runtime (S3c / S7). Present only when is_puppet.
            // puppet_source is borrowed from the asset's PuppetData (stable while
            // realized). Each frame: step_puppet_physics writes puppet_params from
            // a breathing anchor, evaluate_puppet_deform + build_puppet_draw_list
            // rebuild the moving pose, every Part's placement root constants are
            // repacked, and a Part's WriteFrequent vertex buffer is re-uploaded
            // only when its mesh actually moved (per-buffer GPU flush -- see
            // issue #278). The fit-to-target placement is recomputed per render from
            // puppet_bounds + the target size (#280), not stored here.
            const wz::engine::assets::inochi::Puppet* puppet_source = nullptr;
            wz::engine::assets::inochi::PuppetParams puppet_params;
            wz::engine::assets::inochi::PuppetPhysics puppet_physics;
            // The deform the evaluator produces at DEFAULT params, captured at
            // realize. Inochi's base mesh IS the default pose, so the per-frame
            // deform is taken RELATIVE to this baseline (pose - baseline) -- at
            // default params that is zero (the rest mesh renders unchanged), and
            // only deviations (physics, breathing) move it. Removes a large static
            // rest deform the raw evaluator otherwise applies.
            wz::engine::assets::inochi::PuppetDeform puppet_deform_baseline;
            // Animation-clock bookkeeping (#282). The pose is rebuilt on EVERY
            // render (the placement depends on the target size), but the
            // physics may only integrate time that has actually passed: a
            // second render of the same puppet in the same frame -- the RTT
            // pass in the puppet showcase -- must observe the same instant, not
            // step the pendulums again. puppet_clock_seconds is the instant the
            // physics has been stepped to; puppet_physics_debt is the
            // not-yet-consumed remainder of a fixed-step accumulator, which
            // keeps the integrator's step size (and so its stability) identical
            // to the old hardcoded 1/60 on any refresh rate.
            double puppet_clock_seconds = 0.0;
            bool puppet_clock_started = false;
            float puppet_physics_debt = 0.0f;
            // Puppet-space rest bounds (captured at realize). Used to recompute the
            // fit-to-target placement per render for the CURRENT target size (#280,
            // so RTT into an arbitrary-sized texture lands), and to scale the
            // breathing amplitude with the puppet's height (puppets are thousands of
            // px tall, so a fixed pixel amplitude is imperceptible).
            std::array<float, 2> puppet_bounds_min{};
            std::array<float, 2> puppet_bounds_max{};
            struct PuppetPartRuntime
            {
                std::size_t node_index = 0;
                // This Part's slice of the puppet's SHARED buffers (#278); both
                // ride the per-Part root constants so the VS can offset its pull.
                uint32_t vertex_base = 0;
                uint32_t index_base = 0;
                uint32_t vertex_count = 0;
                // Last per-vertex offsets uploaded to `vertices`. Each upload is a
                // synchronous GPU flush, so the per-frame update re-uploads only
                // when the new offsets differ from these past a sub-pixel epsilon.
                // Most Parts sit at the baseline (zero) and never re-upload; subtle
                // motion re-uploads only when it crosses the threshold -- so a
                // puppet stops stalling the frame per Part every frame (#278).
                std::vector<std::array<float, 2>> last_offsets;
                bool ever_uploaded = false;
            };
            std::vector<PuppetPartRuntime> puppet_part_runtime;
            // The puppet's ONE shared vertex buffer (#278). A deform rebuilds the
            // whole concatenated array and uploads it once, instead of one
            // synchronous GPU flush per moving Part.
            wz::rhi::GpuResourceHandle puppet_vertices{};
            std::vector<wz::engine::assets::inochi::PuppetVertex>
                puppet_vertex_scratch;

            // Mask compositing (#275). One render-target texture per DISTINCT
            // mask-source node: a prepass draws that source alone into it, and
            // every Part masked by it samples the result's alpha as coverage.
            //
            // Sized to the RENDER TARGET, because the masked Part samples with
            // its own target-space position -- the mask must be rasterised
            // through the same puppet->target placement, and that placement is
            // recomputed per target (#280). So a target-size change invalidates
            // them: puppet_mask_size records what they were built for, and they
            // are rebuilt (and the Part SRGs rebound) when it no longer matches.
            struct PuppetMaskTarget
            {
                std::size_t source_node = 0;      // index into Puppet::nodes
                std::size_t source_packet = 0;    // its packet, for the prepass
                wz::rhi::ResourceIdentity identity{};
                wz::rhi::GpuResourceHandle resource{};  // SRG binding
                wz::gpu::GPUHandle render_target{};     // begin_offscreen_pass
            };
            std::vector<PuppetMaskTarget> puppet_mask_targets;
            std::array<uint32_t, 2> puppet_mask_size{ 0u, 0u };
            // Parallel to puppet_packets: which mask target each Part samples,
            // or kNoMaskTarget for an unmasked Part (which binds the puppet's
            // 1x1 white texture instead).
            static constexpr std::size_t kNoMaskTarget =
                static_cast<std::size_t>(-1);
            std::vector<std::size_t> puppet_part_mask_target;

            // Star-field renderables (#266) bind the resident star point
            // StructuredBuffer (StarCatalog semantic, asset-owned) and record a
            // non-indexed draw of 6 * star_count vertices. is_star_field gates the
            // per-frame SplatCloudDrawConstants packing (reused verbatim) in
            // render_scene; star_settings.star_size drives the billboard size.
            bool is_star_field = false;
            wz::engine::assets::StarFieldRenderSettings star_settings{};

            // Baked mesh-render-style shading (issue #195 slice A). When
            // has_style is set, render_scene packs MeshStyleDrawConstants (MVP +
            // 12 floats of style) into this renderable's root constants instead of
            // the plain 16-float MVP, and the program declares the "mesh_style"
            // root constant (binding_layout preset 4). Only the CPU pull-mesh path
            // sets it; clipmap/splat/gpu_sparse take their own pack branches.
            bool has_style = false;
            wz::engine::assets::MeshRenderStyleShading style{};

            // Custom renderable recipes (issue #228). is_custom gates the
            // generic per-frame constants pack: custom_constants is the full
            // root-constant block as a byte template — authored tail values
            // baked in at realize time — and render_scene re-packs only the
            // head packer named by custom_constants_head (Mvp16 /
            // WorldViewProjCamera36) in place each frame. The recipe's extra
            // resource bindings were walked into object_srg at realize time;
            // they are asset-owned (bound, never released).
            bool is_custom = false;
            wz::engine::assets::RenderBindingConstantsHead
                custom_constants_head =
                    wz::engine::assets::RenderBindingConstantsHead::None;
            std::vector<uint8_t> custom_constants;
            // The recipe's declared tail-field table (name → baked offset /
            // width; #229). Per-instance scene-node constant overrides merge
            // against it by name at pack time — into the PACKET's copy of the
            // block, never this shared template, so two nodes drawing the
            // same recipe can carry different override values.
            std::vector<wz::engine::assets::RhiRenderableConstant>
                custom_fields;
        };

        const RealizedProgram* realize_program(
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& program_key);
        RealizedRenderable* ensure_renderable(
            wz::engine::assets::EngineAssetLibrary& assets,
            const wz::asset::AssetKey& renderable_key);
        // Verify the shader's rhi ShaderModule (produced by the shader compiler
        // under shader_ref(key)) is registered. The renderer never compiles
        // shaders; this just gates the render-time program fallback (#193).
        bool ensure_shader_module(const wz::asset::AssetKey& shader_key);

        // Mask compositing prepass (#275). Ensures `realized`'s mask targets
        // exist at target_w/target_h (rebuilding them, and rebinding the Part
        // SRGs that sample them, whenever the target size changed), then renders
        // each distinct mask source alone into its target.
        //
        // MUST run BEFORE the frame's main pass is begun: each mask is its own
        // offscreen pass, and passes do not nest. Returns false if a mask target
        // could not be acquired -- the Parts then sample the 1x1 white texture
        // and draw unmasked, which is the pre-#275 behaviour.
        bool render_puppet_masks(
            RealizedRenderable& realized, uint32_t target_w, uint32_t target_h);

        // Per-frame puppet deform + physics (S3c/S7). Advances the SimplePhysics
        // pendulums from a subtle breathing anchor, rebuilds the deformed pose,
        // repacks each Part's placement root constants, and re-uploads a Part's
        // WriteFrequent vertex buffer only when its mesh actually moved this frame.
        // Called from the per-frame packing loop for an is_puppet renderable;
        // target_w/target_h are the current render target's dimensions (backbuffer
        // or offscreen texture) so the fit-to-target placement is recomputed for it.
        void update_puppet_pose(
            RealizedRenderable& realized, uint32_t target_w, uint32_t target_h);

        // Pack a camera-follow terrain draw-constant block (the 32-dword
        // ClipmapDrawConstants: view_projection + per-level snap / world→uv /
        // vertical / texel-extent) into `out`, from the node transform + the
        // realized clipmap settings/dims + the live camera. Shared by the
        // bespoke clipmap (0x708) branch and the generic custom-renderable
        // (0x70A) CameraSnappedTerrain head case (issue #233) so both stay
        // byte-identical.
        void pack_camera_snapped_terrain_constants(
            const RealizedRenderable& realized,
            const wz::math::Mat4& node_world,
            const wz::math::Mat4& view_projection,
            const wz::math::Vec3& camera_world_pos,
            std::vector<uint8_t>& out);

        // Find-or-create the renderer's single view-constants buffer, seeded
        // with the current frame's values. Acquired LAZILY — on the first
        // realize of a program that actually asks for view constants — so a
        // renderer whose programs never do (every one of them today)
        // allocates nothing and its resident_gpu_resource_count is unchanged.
        // Null handle on failure; the caller fails the realize.
        wz::rhi::GpuResourceHandle ensure_view_constants_buffer();

        // The screen-constants twin: the one small buffer behind every Screen
        // view SRG, refreshed in place each frame from the viewport size.
        wz::rhi::GpuResourceHandle ensure_screen_constants_buffer();

        // Point `realized`'s view SRG at that buffer, against the program's
        // slot-0 layout. Binds only where that layout declares the
        // view_constants row; a null slot0_layout, or one without the row, is
        // the NORMAL case and succeeds silently having bound nothing — unlike
        // the slot-2 object group, whose absence is an error. Returns false
        // only when a declared view block could not be satisfied.
        bool bind_view_constants(
            RealizedRenderable& realized,
            const wz::rhi::ShaderResourceGroupLayout* slot0_layout);

        EngineGpuContext&           gpu_;
        wz::Logger&                 logger_;
        RhiContext                  ctx_;
        RhiDx12PipelineCache        cache_;
        RhiDx12CommandRecorder      recorder_;
        wz::rhi::DrawListTag        forward_{};

        std::unordered_map<wz::asset::AssetKey, bool, wz::asset::AssetKeyHash>
            registered_shaders_;
        std::unordered_map<
            wz::asset::AssetKey, RealizedProgram, wz::asset::AssetKeyHash>
            realized_programs_;
        std::unordered_map<
            wz::asset::AssetKey, RealizedRenderable, wz::asset::AssetKeyHash>
            realized_renderables_;
        // Renderables that failed to realize (unresolved/broken graph). Logged
        // once and then skipped each frame so the render loop never re-attempts
        // or re-logs per frame (which floods the editor log sink). Cleared on a
        // graph swap (on_graph_changed) so a fixed graph re-realizes.
        std::unordered_set<wz::asset::AssetKey, wz::asset::AssetKeyHash>
            failed_renderables_;

        // One-shot guard so the clipmap world-placement diagnostic (footprint vs
        // node scale, for aligning a terrain-stick collision) logs once, not per
        // frame.
        bool clipmap_placement_logged_ = false;

        // See render_time_program_bridge_count().
        std::size_t render_time_program_bridges_ = 0;

        // The renderer's animation clock, in seconds. Advanced ONCE per
        // simulation tick by the real frame delta (simulation_tick) and only
        // READ by render_scene -- it is deliberately not a render-call counter
        // (#282). Every offscreen invocation in a frame therefore observes the
        // same instant as the main one, and the rate no longer depends on how
        // many passes the app happens to make or on the display refresh rate.
        // Drives the star-field twinkle phase (#266) and the puppet's breathing
        // bob + physics.
        double animation_seconds_ = 0.0;

        // This frame's view constants, packed at the top of render_scene. Held
        // as state (rather than passed down) because ensure_renderable may
        // acquire the buffer MID-frame, and it must be seeded with the same
        // values the frame's refresh already wrote — otherwise the first draw
        // of a newly realized renderable reads whatever the allocation held.
        ViewConstants view_constants_{};

        // The screen-space twin, filled from the viewport size each frame; bound
        // where a layout declares the Screen view head (2D overlays / HUD).
        ScreenConstants screen_constants_{};

        // The one small buffer behind every view SRG, refreshed in place each
        // frame (the #145 door: acquire WriteOnce, then update). Renderer-owned
        // and graph-INDEPENDENT, so it deliberately survives on_graph_changed:
        // it is keyed by no AssetKey and nothing about a graph swap invalidates
        // the observer. The handle stays stable, so the recorder's cached
        // descriptor tables stay valid across the refresh.
        wz::rhi::GpuResourceHandle view_constants_buffer_{};

        // The screen-constants twin buffer (see ensure_screen_constants_buffer).
        wz::rhi::GpuResourceHandle screen_constants_buffer_{};
    };

    // Compose each scene node's world transform from its local TRS and its
    // parent chain (world = parent_world * local), so renderable children
    // inherit their parents' transforms. Returns one matrix per node,
    // index-aligned with `nodes`. Self-contained: it walks parent_id within
    // `nodes` only and does NOT consult the ECS polytree or the legacy
    // compile_scene path. A node with no parent, a dangling parent id, or a
    // parent cycle resolves to its own local TRS. RhiSceneRenderer::render_scene
    // consumes this so nesting a node moves its children.
    std::vector<wz::math::Mat4> compute_scene_node_world_transforms(
        std::span<const wz::engine::assets::SceneNodeAsset> nodes);

    // Effective (inherited) visibility per node: 1 iff the node AND every
    // ancestor is visible, so hiding a parent hides its whole subtree (e.g. a
    // scene-source host hiding its grafted children). Index-aligned with
    // `nodes`; same self-contained parent_id walk as the world transforms (a
    // dangling/cyclic parent falls back to the node's own visibility).
    std::vector<std::uint8_t> compute_scene_node_effective_visibility(
        std::span<const wz::engine::assets::SceneNodeAsset> nodes);

    // Merge a scene node's per-instance constant overrides (issue #229) into a
    // packed custom root-constant block. `fields` is the recipe's declared
    // tail-field table (every layout field with its baked dword offset/width);
    // each override matches a field by name and its value[0..dwords) replaces
    // the field's bytes in `block`. An override naming no declared field is
    // ignored (the node may momentarily disagree with a re-authored layout —
    // authoring feedback lives at compile/bridge time, not per frame), and
    // writes are clamped to the block. This runs against the DRAW PACKET's
    // copy of the block each frame, which is what makes an override edit a
    // pack-time change: no asset-graph recompile, no re-key.
    void merge_renderable_constant_overrides(
        std::span<uint8_t> block,
        std::span<const wz::engine::assets::RhiRenderableConstant> fields,
        std::span<const wz::engine::assets::SceneRenderableConstantOverride>
            overrides) noexcept;
}
