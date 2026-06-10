#pragma once

// wz/render/frame/render_frame.h

#include <render/ir/render_ir.h>
#include <scene/compile/compiled_scene.h>
#include <stats/scene_render_storage.h>
#include <cstdint>
#include <memory>
#include <span>

namespace wz::render {

    using wz::scene::CompiledSceneView;
    using wz::scene::MeshHandle;
    using wz::scene::MaterialHandle;
    using wz::scene::SplatHandle;
    using wz::scene::INVALID_MESH;
    using wz::scene::INVALID_MATERIAL;
    using wz::scene::INVALID_SPLAT;
    using wz::scene::TerrainVisualInstance;
    using wz::scene::LightRecord;
    using wz::scene::ViewData;


    // ─── Pipeline submission order ────────────────────────────────────────────────
    //
    // Fixed global order. Not negotiable per frame.

    enum class PipelineStage : uint8_t {
        Sky = 0,
        OpaqueGeometry = 1,
        Splat = 2,
        TransparentGeometry = 3,
        Particle = 4,
    };

    enum class SkyVisualKind : uint8_t {
        None = 0,
        SolidColor,
        DirectionDebug,
        Gradient,
        EquirectangularTexture,
        ScalarField,
        VectorField,
    };

    enum class SkyProjection : uint8_t {
        Sphere = 0,
        Dome,
        Cube,
        FullscreenDirectionMap,
    };

    struct SkyDrawCommand {
        PipelineStage stage{ PipelineStage::Sky };
        SkyVisualKind visual_kind{ SkyVisualKind::None };
        SkyProjection projection{ SkyProjection::Sphere };

        wz::math::Vec3 solid_color{ 0.f, 0.f, 0.f };
        wz::math::Vec3 gradient_top_color{ 0.08f, 0.22f, 0.55f };
        wz::math::Vec3 gradient_bottom_color{ 0.62f, 0.78f, 0.95f };
        float exposure{ 1.f };

        float rotation_x_radians{ 0.f };
        float rotation_y_radians{ 0.f };
        float rotation_z_radians{ 0.f };
        float radius{ 1.f };

        // Renderer-local handles for future texture/scalar-field sky visuals.
        // Invalid for the current authored-asset bridge, which only submits
        // SolidColor. Keeping the slots here prevents "sky sphere" from becoming
        // a one-off clear-color feature; it is a generic encompassing projection
        // lane that can later sample textures, fields, or procedural sources.
        uint32_t texture_handle{ 0xFFFF'FFFFu };
        uint32_t scalar_field_handle{ 0xFFFF'FFFFu };
        uint32_t vector_field_handle{ 0xFFFF'FFFFu };
    };


    // ─── DrawCommandKind ──────────────────────────────────────────────────────────
    //
    // Describes what payload the DrawCommand carries.  Orthogonal to PipelineStage:
    // PipelineStage says WHEN to draw; DrawCommandKind says WHAT to draw.
    //
    // Mesh         — mesh + material handles valid (default, backward-compatible)
    // GaussianSplats — splats_buffer handle valid; resolved at submit time via
    //                  RenderResourceResolver
    // ScalarField  — reserved for future use

    enum class DrawCommandKind : uint8_t {
        Mesh = 0,
        GaussianSplats,
        ScalarField,
    };


    // ─── DrawCommand ──────────────────────────────────────────────────────────────
    //
    // A single resolved draw unit. Self-contained — no pointers into
    // CompiledScene or RenderIR. The backend never touches those structures.

    struct DrawCommand {
        PipelineStage   stage{};
        DrawCommandKind kind{ DrawCommandKind::Mesh };

        // Mesh payload (kind == Mesh)
        MeshHandle     mesh{ INVALID_MESH };
        MaterialHandle material{ INVALID_MATERIAL };

        // GaussianSplats payload (kind == GaussianSplats)
        // Index into RenderResourceResolver's splat registry.
        SplatHandle    splats_buffer{ INVALID_SPLAT };

        wz::math::Mat4 world{};
        uint64_t       sort_key{ 0 };

        // Per-cloud splat fields — valid when kind == GaussianSplats and
        // splats_buffer != INVALID_SPLAT (cloud / SplatPull path).
        //
        // splat_instance_count: number of splats to draw in this cloud.
        // sorted_splat_indices: back-to-front sorted indices into the cloud GPU
        //   buffer; backed by RenderFrameStorage::sorted_index_buffer.
        //   Empty span means "use the identity t1 buffer" (no upload needed).
        uint32_t                  splat_instance_count{ 0 };
        std::span<const uint32_t> sorted_splat_indices{};

        // Per-primitive splat fields — valid only when stage == Splat and
        // splats_buffer == INVALID_SPLAT (legacy per-primitive path).
        wz::math::Vec3 splat_position{};
        wz::math::Vec3 splat_scale{ 1.f, 1.f, 1.f };
        wz::math::Vec4 splat_rotation{ 0.f, 0.f, 0.f, 1.f };
        wz::math::Vec3 splat_color{ 1.f, 1.f, 1.f };
        float splat_opacity{ 1.f };
        float splat_depth{ 0.f };
    };


    // ─── RenderFrameView ─────────────────────────────────────────────────────────
    //
    // Non-owning view into RenderFrameStorage. Sectioned by update frequency:
    //
    //   opaque      — rebuilt by build_frame() and update_frame_view() (frustum
    //                 culling changes the visible set on camera moves).
    //
    //   terrain      \
    //   splats        \
    //   transparent    > view-dependent; rebuilt by build_frame() and update_frame_view().
    //   particles     /
    //
    // Submission order convention: sky -> opaque -> terrain -> splats ->
    // transparent -> particles. The backend iterates the spans in that order.
    //
    // Each pipeline's draws are already sorted by the RenderIR layer:
    //   opaque      — front-to-back by material (minimises state changes)
    //   splats      — back-to-front by depth
    //   transparent — back-to-front by depth
    //   particles   — back-to-front by depth

    struct RenderFrameView {
        // Stable section — authored/global presentation surfaces. Drawn before
        // regular scene geometry; v0 backends may implement SolidColor as a
        // render-target clear while richer projections become a real sky pass.
        std::span<const SkyDrawCommand> sky;

        // Stable section — backed by RenderFrameStorage::stable_buffer
        std::span<const DrawCommand> opaque;
        std::span<const TerrainVisualInstance> terrain_instances;

        // View-dependent section — backed by RenderFrameStorage::view_buffer
        // Layout within view_buffer: [ terrain | splats | transparent | particles ]
        std::span<const TerrainDrawRef> terrain;
        std::span<const DrawCommand> splats;
        std::span<const DrawCommand> transparent;
        std::span<const DrawCommand> particles;

        std::span<const LightRecord> lights;

        ViewData view{};
    };


    // ─── RenderFrameStorage ───────────────────────────────────────────────────────
    //
    // Two separate allocations matching the update-frequency split in RenderFrameView:
    //
    //   stable_buffer — opaque commands; sized for total opaque count so
    //                   update_frame_view() can rewrite the visible subset in-place
    //                   without reallocating when the frustum changes.
    //   view_buffer   — terrain refs + splat/transparent/particle commands; rebuilt every
    //                   camera move by update_frame_view().

    struct RenderFrameStorage {
        std::unique_ptr<std::byte[]> stable_buffer;
        size_t                       stable_bytes    = 0;
        uint32_t                     sky_capacity    = 0;
        uint32_t                     opaque_capacity = 0; // total slots carved in stable_buffer
        uint32_t                     terrain_instance_capacity = 0;

        std::unique_ptr<std::byte[]> view_buffer;
        size_t                       view_bytes = 0;

        // Backing store for sorted_splat_indices spans in per-cloud DrawCommands.
        // Sized for the maximum visible splat count.  Reused frame-to-frame.
        std::unique_ptr<uint32_t[]>  sorted_index_buffer;
        size_t                       sorted_index_capacity = 0;

        wz::scene_render::AllocationStats stable_stats{};
        wz::scene_render::AllocationStats view_stats{};

        RenderFrameView frame;
    };


    // ─── build_frame() ───────────────────────────────────────────────────────────
    //
    // Full rebuild — both stable and view-dependent sections.
    // Fills storage; returns a view into it.
    // Submission order: sky -> opaque -> terrain -> splats -> transparent -> particles.
    // Each pipeline's draws are already sorted by the IR layer.

    RenderFrameView build_frame(
        RenderFrameStorage& storage,
        RenderIRView        ir,
        CompiledSceneView   scene,
        std::span<const SkyDrawCommand> sky = {});


    // ─── update_frame_view() ─────────────────────────────────────────────────────
    //
    // Rebuilds the opaque section in-place (frustum culling may expose new opaques)
    // and rebuilds the view-dependent section (terrain, splats, transparent, particles).
    //
    // Call after update_view() + update_render_ir() when the camera moved but
    // opaque geometry/descriptors are unchanged.
    //
    // Does not reallocate stable_buffer (pre-sized for worst-case in build_frame()).
    // May reallocate view_buffer if the view-dependent count grew, but this is rare.

    RenderFrameView update_frame_view(
        RenderFrameStorage& storage,
        RenderIRView        ir,
        CompiledSceneView   scene);

} // namespace wz::render
