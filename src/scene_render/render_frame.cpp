// file: src/scene_render/render_frame.cpp

#include <render/frame/render_frame.h>
#include <algo/next.h>
#include <algorithm>

namespace wz::render {

    using namespace wz::scene;
    using namespace wz::math;

    namespace detail {

        // ── Output sink ──────────────────────────────────────────────────────────────
        //
        // Thin wrapper satisfying the wz::core::algo::next output contract
        // (push(v) -> bool), backed by a pre-allocated span so no heap
        // allocation occurs during frame building.
        //
        // DrawCommandSink  — writes DrawCommands via placement-new into a pre-carved
        //                    byte span.  Safe on both uninitialised raw memory
        //                    (build_frame) and previously-constructed DrawCommand
        //                    objects (update_frame_view), because DrawCommand has only
        //                    trivially-destructible members.

        struct DrawCommandSink {
            DrawCommand* ptr;
            uint32_t     count    = 0;
            uint32_t     capacity = 0;

            explicit DrawCommandSink(std::span<DrawCommand> s) noexcept
                : ptr(s.data()), capacity(static_cast<uint32_t>(s.size())) {}

            bool push(DrawCommand cmd) noexcept {
                if (count >= capacity) return false;
                new (&ptr[count++]) DrawCommand(std::move(cmd));
                return true;
            }
        };

        struct CloudSplatCommandSink {
            DrawCommandSink         commands;
            const CompiledSceneView& cs;

            bool push(const DrawRef& ref) noexcept {
                const auto& s = cs.splats[ref.index];
                Mat4 world = Mat4::identity();
                world.m[12] = s.position.x;
                world.m[13] = s.position.y;
                world.m[14] = s.position.z;
                return commands.push(DrawCommand{
                    .stage         = PipelineStage::Splat,
                    .kind          = DrawCommandKind::GaussianSplats,
                    .splats_buffer = s.cloud_handle,
                    .world         = world,
                    .sort_key      = ref.sort_key,
                });
            }

            uint32_t count() const noexcept { return commands.count; }
        };


        // ── Fill splat DrawCommands ───────────────────────────────────────────────
        //
        // Two paths, chosen via a reduce over ir.splats:
        //
        //   Cloud-instance path  (any splat has cloud_handle != INVALID_SPLAT):
        //     One DrawCommand per visible SplatPrimitive with a valid cloud handle.
        //     Each primitive represents a scene node that instances an entire
        //     splat cloud.  The backend draws the full cloud resource using the
        //     primitive's world transform (splat_instance_count=0, no sorted
        //     indices).
        //
        //   Legacy path (all cloud_handles == INVALID_SPLAT):
        //     transform over ir.splats to emit one DrawCommand per splat with
        //     per-primitive data (backward-compatible).
        //
        // The heavy loops (over potentially thousands of DrawRefs) are expressed as
        // library calls so future parallelisation / SIMD lives in algo::next, not here.
        //
        // Returns the number of DrawCommands actually written (≤ splats_w.size()).

        uint32_t fill_splat_commands(
            std::span<DrawCommand>   splats_w,
            uint32_t*                sorted_index_buf,
            const RenderIRView&      ir,
            const CompiledSceneView& cs)
        {
            using namespace wz::core::algo::next;

            // ── Detect cloud path via reduce ─────────────────────────────────────
            const bool any_cloud = reduce(
                ir.splats,
                false,
                [&cs](bool acc, const DrawRef& ref) -> bool {
                    return acc || cs.splats[ref.index].cloud_handle != INVALID_SPLAT;
                });

            // ── Legacy path: one DrawCommand per splat ───────────────────────────
            if (!any_cloud) {
                DrawCommandSink sink{ splats_w };
                transform(ir.splats, sink,
                    [&cs](const DrawRef& ref) -> DrawCommand {
                        const auto& s = cs.splats[ref.index];
                        return DrawCommand{
                            .stage          = PipelineStage::Splat,
                            .mesh           = INVALID_MESH,
                            .material       = INVALID_MATERIAL,
                            .sort_key       = ref.sort_key,
                            .splat_position = s.position,
                            .splat_scale    = s.scale,
                            .splat_rotation = s.rotation,
                            .splat_color    = s.color,
                            .splat_opacity  = s.opacity,
                            .splat_depth    = cs.splat_depths[ref.index],
                        };
                    });
                return sink.count;
            }

            // ── Cloud-instance path ──────────────────────────────────────────────
            //
            // One DrawCommand per SplatPrimitive with a valid cloud_handle.
            // Each primitive represents a scene node that instances an entire
            // splat cloud at a given world position.  The backend draws the full
            // cloud resource (splat_instance_count=0, empty sorted_splat_indices)
            // using the per-instance world transform.

            CloudSplatCommandSink cmd_sink{ DrawCommandSink{ splats_w }, cs };
            filter(ir.splats, cmd_sink,
                [&cs](const DrawRef& ref) -> bool {
                    return cs.splats[ref.index].cloud_handle != INVALID_SPLAT;
                });

            return cmd_sink.count();
        }


        // ── Fill view-dependent sections ──────────────────────────────────────────
        //
        // Writes splat, transparent, and particle DrawCommands into pre-carved spans
        // using transform, delegating heavy iteration to algo::next.
        // Returns the actual number of splat DrawCommands written.

        uint32_t fill_view_sections(
            std::span<DrawCommand>   splats_w,
            std::span<DrawCommand>   transparent_w,
            std::span<DrawCommand>   particles_w,
            uint32_t*                sorted_index_buf,
            const RenderIRView&      ir,
            const CompiledSceneView& cs)
        {
            using namespace wz::core::algo::next;

            const uint32_t splat_cmds =
                fill_splat_commands(splats_w, sorted_index_buf, ir, cs);

            DrawCommandSink tr_sink{ transparent_w };
            transform(ir.transparent, tr_sink,
                [&cs](const DrawRef& ref) -> DrawCommand {
                    const auto& p = cs.transparent[ref.index];
                    return DrawCommand{
                        .stage    = PipelineStage::TransparentGeometry,
                        .mesh     = p.mesh,
                        .material = p.material,
                        .world    = p.world,
                        .sort_key = ref.sort_key,
                    };
                });

            DrawCommandSink pa_sink{ particles_w };
            transform(ir.particles, pa_sink,
                [&cs](const DrawRef& ref) -> DrawCommand {
                    const auto& p = cs.particles[ref.index];
                    return DrawCommand{
                        .stage    = PipelineStage::Particle,
                        .mesh     = p.mesh,
                        .material = p.material,
                        .world    = p.world,
                        .sort_key = ref.sort_key,
                    };
                });

            return splat_cmds;
        }


        // ── Allocate/reuse view_buffer and carve the three view-dependent spans ───
        //
        // splat_count is the upper bound for the splat section (ir.splats.size()).
        // sorted_index_buffer is grown here to hold splat_count uint32_t entries.

        void prepare_view_buffer(
            RenderFrameStorage&     storage,
            uint32_t                splat_count,
            uint32_t                transparent_count,
            uint32_t                particle_count,
            std::span<DrawCommand>& splats_w,
            std::span<DrawCommand>& transparent_w,
            std::span<DrawCommand>& particles_w)
        {
            const size_t view_size =
                sizeof(DrawCommand) * (splat_count + transparent_count + particle_count)
                + alignof(DrawCommand);

            storage.view_stats.reset_build_counters();
            storage.view_stats.record_owned(storage.view_bytes);

            if (view_size > storage.view_bytes) {
                storage.view_buffer = std::make_unique<std::byte[]>(view_size);
                storage.view_bytes  = view_size;
                storage.view_stats.record_reallocation(view_size);
            } else {
                storage.view_stats.record_owned(storage.view_bytes);
            }

            std::byte* vp = storage.view_buffer.get();
            std::byte* ve = vp + view_size;
            vp = wz::core::graph::detail::carve<DrawCommand>(vp, ve, splat_count,       splats_w);
            vp = wz::core::graph::detail::carve<DrawCommand>(vp, ve, transparent_count, transparent_w);
            vp = wz::core::graph::detail::carve<DrawCommand>(vp, ve, particle_count,    particles_w);

            if (splat_count > storage.sorted_index_capacity) {
                storage.sorted_index_buffer   = std::make_unique<uint32_t[]>(splat_count);
                storage.sorted_index_capacity = splat_count;
            }
        }

    } // namespace detail


    // ─── build_frame() ───────────────────────────────────────────────────────────

    RenderFrameView build_frame(
        RenderFrameStorage& storage,
        RenderIRView        ir,
        CompiledSceneView   scene,
        std::span<const SkyDrawCommand> sky)
    {
        using namespace wz::core::algo::next;

        const CompiledSceneView& cs = scene;

        const uint32_t sky_total         = static_cast<uint32_t>(sky.size());
        const uint32_t opaque_total      = static_cast<uint32_t>(cs.opaque.size());
        const uint32_t splat_count       = static_cast<uint32_t>(ir.splats.size());
        const uint32_t transparent_count = static_cast<uint32_t>(ir.transparent.size());
        const uint32_t particle_count    = static_cast<uint32_t>(ir.particles.size());

        // ── Stable buffer (opaque) ────────────────────────────────────────────────
        // Sized for total scene opaque count so update_frame_view() can rewrite the
        // visible subset without reallocating when the frustum changes.

        const size_t stable_size =
            sizeof(SkyDrawCommand) * sky_total
            + sizeof(DrawCommand) * opaque_total
            + (std::max)(alignof(SkyDrawCommand), alignof(DrawCommand));

        storage.stable_stats.reset_build_counters();
        storage.stable_stats.record_owned(storage.stable_bytes);

        if (stable_size > storage.stable_bytes) {
            storage.stable_buffer = std::make_unique<std::byte[]>(stable_size);
            storage.stable_bytes  = stable_size;
            storage.stable_stats.record_reallocation(stable_size);
        } else {
            storage.stable_stats.record_owned(storage.stable_bytes);
        }

        std::byte* sp = storage.stable_buffer.get();
        std::byte* se = sp + stable_size;

        std::span<SkyDrawCommand> sky_w;
        std::span<DrawCommand> opaque_w;
        sp = wz::core::graph::detail::carve<SkyDrawCommand>(sp, se, sky_total, sky_w);
        sp = wz::core::graph::detail::carve<DrawCommand>(sp, se, opaque_total, opaque_w);
        storage.sky_capacity = sky_total;
        storage.opaque_capacity = opaque_total;

        for (uint32_t i = 0; i < sky_total; ++i)
            new (&sky_w[i]) SkyDrawCommand(sky[i]);

        // Fill the visible opaque subset via transform.
        // ir.opaque is already sorted front-to-back by material key by the IR layer.
        detail::DrawCommandSink opq_sink{ opaque_w };
        transform(ir.opaque, opq_sink,
            [&cs](const DrawRef& ref) -> DrawCommand {
                const auto& p = cs.opaque[ref.index];
                return DrawCommand{
                    .stage    = PipelineStage::OpaqueGeometry,
                    .mesh     = p.mesh,
                    .material = p.material,
                    .world    = p.world,
                    .sort_key = ref.sort_key,
                };
            });

        // ── View buffer (splats, transparent, particles) ──────────────────────────

        std::span<DrawCommand> splats_w, transparent_w, particles_w;
        detail::prepare_view_buffer(storage, splat_count, transparent_count, particle_count,
                                    splats_w, transparent_w, particles_w);
        const uint32_t actual_splat_cmds = detail::fill_view_sections(
            splats_w, transparent_w, particles_w,
            storage.sorted_index_buffer.get(), ir, cs);

        storage.frame = RenderFrameView{
            .sky         = sky_w,
            .opaque      = opaque_w.subspan(0, opq_sink.count),
            .splats      = splats_w.subspan(0, actual_splat_cmds),
            .transparent = transparent_w,
            .particles   = particles_w,
            .lights      = scene.lights,
            .view        = scene.view,
        };
        return storage.frame;
    }


    // ─── update_frame_view() ─────────────────────────────────────────────────────

    RenderFrameView update_frame_view(
        RenderFrameStorage& storage,
        RenderIRView        ir,
        CompiledSceneView   scene)
    {
        using namespace wz::core::algo::next;

        const CompiledSceneView& cs = scene;

        const uint32_t splat_count       = static_cast<uint32_t>(ir.splats.size());
        const uint32_t transparent_count = static_cast<uint32_t>(ir.transparent.size());
        const uint32_t particle_count    = static_cast<uint32_t>(ir.particles.size());

        // Rebuild opaque section in-place — stable_buffer was pre-sized for the
        // total opaque count in build_frame() so no reallocation is needed.
        // DrawCommand objects were constructed by build_frame(); placement-new
        // via DrawCommandSink is safe since DrawCommand is trivially destructible.
        auto opaque_w = std::span<DrawCommand>(
            const_cast<DrawCommand*>(storage.frame.opaque.data()),
            storage.opaque_capacity);

        detail::DrawCommandSink opq_sink{ opaque_w };
        transform(ir.opaque, opq_sink,
            [&cs](const DrawRef& ref) -> DrawCommand {
                const auto& p = cs.opaque[ref.index];
                return DrawCommand{
                    .stage    = PipelineStage::OpaqueGeometry,
                    .mesh     = p.mesh,
                    .material = p.material,
                    .world    = p.world,
                    .sort_key = ref.sort_key,
                };
            });
        storage.frame.opaque = opaque_w.subspan(0, opq_sink.count);

        // Rebuild view buffer (splats, transparent, particles).
        std::span<DrawCommand> splats_w, transparent_w, particles_w;
        detail::prepare_view_buffer(storage, splat_count, transparent_count, particle_count,
                                    splats_w, transparent_w, particles_w);
        const uint32_t actual_splat_cmds = detail::fill_view_sections(
            splats_w, transparent_w, particles_w,
            storage.sorted_index_buffer.get(), ir, cs);

        storage.frame.splats      = splats_w.subspan(0, actual_splat_cmds);
        storage.frame.transparent = transparent_w;
        storage.frame.particles   = particles_w;
        storage.frame.lights      = scene.lights;
        storage.frame.view        = scene.view;
        return storage.frame;
    }

} // namespace wz::render
