#include <gtest/gtest.h>
#include <scene/compile/scene_compiler.h>
#include <scene/compile/legacy_classification.h>
#include <render/ir/render_ir.h>
#include <render/frame/render_frame.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace wz::scene;
using namespace wz::render;
using namespace wz::core::graph;
using namespace wz::math;

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

    AABB unit_box()
    {
        return { Vec3{ -0.5f, -0.5f, -0.5f }, Vec3{ 0.5f, 0.5f, 0.5f } };
    }

    // Wide orthographic VP: frustum ≈ ±10000 — all objects visible regardless of position.
    ViewData identity_view()
    {
        ViewData v{};
        v.view            = Mat4::identity();
        v.projection      = Mat4::identity();
        v.view_projection.m[0]  = 1e-4f;
        v.view_projection.m[5]  = 1e-4f;
        v.view_projection.m[10] = 1e-4f;
        v.view_projection.m[15] = 1.0f;
        return v;
    }

    // Identity VP (unit-cube frustum ≈ ±1 in world space).
    // Objects at z=0 with unit_box are inside; objects at z=100 are outside.
    ViewData unit_frustum_view()
    {
        ViewData v{};
        v.view            = Mat4::identity();
        v.projection      = Mat4::identity();
        v.view_projection = Mat4::identity();
        return v;
    }

    Mat4 translation_z(float z)
    {
        Mat4 m = Mat4::identity();
        m.m[14] = z;
        return m;
    }

    void set_world(SceneGraph& g, NodeHandle n, const Mat4& world)
    {
        const_cast<TransformNode&>(node_data(g, n)).world = world;
    }

    // ── Mixed scene ────────────────────────────────────────────────────────────
    //
    // Flat scene under one root with four separate pipeline groups:
    //   opaque      — mesh/material pair varied by index (drives material sort keys)
    //   transparent — unit mesh, sorted by depth
    //   splats      — no mesh
    //   particles   — unit mesh, sorted by depth

    struct BenchScene
    {
        SceneStorage                      storage{};
        std::vector<RenderableDescriptor> descs{};
        std::vector<NodeHandle>           opaque{};
        std::vector<NodeHandle>           transparent{};
        std::vector<NodeHandle>           splats{};
        std::vector<NodeHandle>           particles{};
    };

    BenchScene make_bench_scene(uint32_t n_opaque,
                                uint32_t n_transparent,
                                uint32_t n_splat,
                                uint32_t n_particle)
    {
        SceneBuilder b;

        TransformNode root_node{};
        root_node.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root_node);

        auto make_node = [&](float z) -> NodeHandle {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = TransformNodeFlag::RenderDomain;
            NodeHandle h = add_node(b, n);
            EXPECT_TRUE(add_edge(b, root_h, h));
            return h;
        };

        std::vector<NodeHandle> opaque_h, transparent_h, splat_h, particle_h;
        opaque_h.reserve(n_opaque);
        transparent_h.reserve(n_transparent);
        splat_h.reserve(n_splat);
        particle_h.reserve(n_particle);

        for (uint32_t i = 0; i < n_opaque;      ++i) opaque_h.push_back(make_node(static_cast<float>(i)));
        for (uint32_t i = 0; i < n_transparent;  ++i) transparent_h.push_back(make_node(static_cast<float>(i) + 0.5f));
        for (uint32_t i = 0; i < n_splat;        ++i) splat_h.push_back(make_node(static_cast<float>(i) + 0.25f));
        for (uint32_t i = 0; i < n_particle;     ++i) particle_h.push_back(make_node(static_cast<float>(i) + 0.75f));

        auto result = build(std::move(b));
        EXPECT_TRUE(result.has_value());

        BenchScene out{};
        out.storage     = std::move(*result);
        out.opaque      = std::move(opaque_h);
        out.transparent = std::move(transparent_h);
        out.splats      = std::move(splat_h);
        out.particles   = std::move(particle_h);

        const uint32_t total = node_count(out.storage.polytree);
        out.descs.resize(total);

        out.descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };

        for (uint32_t i = 0; i < n_opaque; ++i) {
            NodeHandle h = out.opaque[i];
            out.descs[h] = RenderableDescriptor{
                .node_class   = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                .mesh         = (i % 10) + 1,
                .material     = (i % 5)  + 1,
                .local_bounds = unit_box(),
            };
        }

        for (uint32_t i = 0; i < n_transparent; ++i) {
            NodeHandle h = out.transparent[i];
            out.descs[h] = RenderableDescriptor{
                .node_class   = classify_legacy_renderable(RenderPipeline::TransparentGeometry),
                .mesh         = 1u,
                .material     = 1u,
                .local_bounds = unit_box(),
            };
        }

        for (uint32_t i = 0; i < n_splat; ++i) {
            NodeHandle h = out.splats[i];
            out.descs[h] = RenderableDescriptor{
                .node_class = classify_legacy_renderable(RenderPipeline::Splat),
                .splat_data = SplatDescriptor{},
            };
        }

        for (uint32_t i = 0; i < n_particle; ++i) {
            NodeHandle h = out.particles[i];
            out.descs[h] = RenderableDescriptor{
                .node_class   = classify_legacy_renderable(RenderPipeline::Particle),
                .mesh         = 1u,
                .material     = 1u,
                .local_bounds = unit_box(),
            };
        }

        propagate_all(out.storage.polytree);
        return out;
    }

    // ── Visibility-ratio scene ─────────────────────────────────────────────────
    //
    // n_visible opaque objects at z=0 (inside the unit-cube frustum).
    // n_culled  opaque objects at z=100 (outside the unit-cube frustum).
    // Use with unit_frustum_view() and identity VP for culling benchmarks.

    struct VisibilityScene
    {
        SceneStorage                      storage{};
        std::vector<RenderableDescriptor> descs{};
        uint32_t                          n_visible = 0;
        uint32_t                          n_culled  = 0;
    };

    VisibilityScene make_visibility_scene(uint32_t n_visible, uint32_t n_culled)
    {
        SceneBuilder b;

        TransformNode root_node{};
        root_node.local = Mat4::identity();
        NodeHandle root_h = add_node(b, root_node);

        auto make_opaque = [&](float z) -> NodeHandle {
            TransformNode n{};
            n.local = translation_z(z);
            n.flags = TransformNodeFlag::RenderDomain;
            NodeHandle h = add_node(b, n);
            EXPECT_TRUE(add_edge(b, root_h, h));
            return h;
        };

        std::vector<NodeHandle> in_handles, out_handles;
        in_handles.reserve(n_visible);
        out_handles.reserve(n_culled);

        for (uint32_t i = 0; i < n_visible; ++i)  in_handles.push_back(make_opaque(0.f));
        for (uint32_t i = 0; i < n_culled;  ++i) out_handles.push_back(make_opaque(100.f));

        auto result = build(std::move(b));
        EXPECT_TRUE(result.has_value());

        VisibilityScene out{};
        out.storage   = std::move(*result);
        out.n_visible = n_visible;
        out.n_culled  = n_culled;

        const uint32_t total = node_count(out.storage.polytree);
        out.descs.resize(total);
        out.descs[root_h] = { classify_legacy_renderable(RenderPipeline::None) };

        for (uint32_t i = 0; i < n_visible; ++i) {
            NodeHandle h = in_handles[i];
            out.descs[h] = RenderableDescriptor{
                .node_class   = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                .mesh         = (i % 10) + 1,
                .material     = (i % 5)  + 1,
                .local_bounds = unit_box(),
            };
        }
        for (uint32_t i = 0; i < n_culled; ++i) {
            NodeHandle h = out_handles[i];
            out.descs[h] = RenderableDescriptor{
                .node_class   = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
                .mesh         = (i % 10) + 1,
                .material     = (i % 5)  + 1,
                .local_bounds = unit_box(),
            };
        }

        propagate_all(out.storage.polytree);
        return out;
    }

} // namespace


// ─── DISABLED: full pipeline stage-breakdown benchmark ────────────────────────
//
// Run with --gtest_also_run_disabled_tests.
//
// Scene: N_OPAQUE opaque + N_DEPTH transparent + N_DEPTH splats + N_DEPTH particles.
// M_SLOW iterations for expensive rebuilds; M_FAST for incremental updates.
//
// Measured stages, in order:
//   compile()                            — full rebuild (all three buffers)
//   update_view()                        — view-buffer only; O(transparent+splats+particles)
//   update_compiled_transforms() all     — all nodes dirty, calls update_view()
//   update_compiled_transforms() 10% dirty, vc=true
//   update_compiled_transforms() 10% dirty, vc=false (inline depths, camera stationary)
//   update_compiled_transforms()  1% dirty, vc=true
//   update_compiled_transforms()  1% dirty, vc=false (inline depths, camera stationary)
//   update_render_ir()                   — recull + re-sort all pipelines after update_view()
//   build_render_ir()                    — full IR rebuild (alloc + cull + sort all pipelines)
//   build_frame()                        — full DrawCommand rebuild (both sections)
//   update_frame_view()                  — view-dependent section only (splats/transparent/particles)
//
// Visibility ratio benchmarks (100k opaque, unit-cube frustum):
//   build_render_ir() / update_render_ir() at 100%, 50%, 10%, 1% visible
//   build_frame() time at each ratio (scales with visible count)

TEST(PipelineBenchmark, AllStages)
{
    constexpr uint32_t N_OPAQUE      = 100'000;
    constexpr uint32_t N_DEPTH       =   1'000; // transparent, splats, particles each
    constexpr uint32_t M_SLOW        = 10;
    constexpr uint32_t M_FAST        = 50;

    auto scene = make_bench_scene(N_OPAQUE, N_DEPTH, N_DEPTH, N_DEPTH);
    const uint32_t total_nodes = node_count(scene.storage.polytree);
    const ViewData view        = identity_view();

    // Pre-set world matrices — update_compiled_transforms() reads these every call
    for (uint32_t i = 0; i < N_OPAQUE; ++i)
        set_world(scene.storage.polytree, scene.opaque[i],
                  translation_z(static_cast<float>(i) * 0.1f));

    // ── Allocate and warm all three pipeline storages ─────────────────────────

    CompiledSceneStorage cs{};
    RenderIRStorage      ir_st{};
    RenderFrameStorage   fr_st{};

    compile(cs, scene.storage.polytree, scene.descs, {}, view);
    build_render_ir(ir_st, cs.scene);
    build_frame(fr_st, ir_st.ir, cs.scene);

    // ── Benchmark harness ─────────────────────────────────────────────────────

    auto bench = [](const char* label, uint32_t M, auto fn) {
        fn();
        auto t0 = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < M; ++i) fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / M;
        std::printf("  %-55s  %8.1f us/call\n", label, us);
    };

    std::printf(
        "\nPipeline benchmark  "
        "(%u opaque / %u transparent / %u splats / %u particles,  %u total nodes)\n",
        N_OPAQUE, N_DEPTH, N_DEPTH, N_DEPTH, total_nodes);
    std::printf("  %-55s  %8s\n", "Stage", "avg µs");
    std::printf("  %s\n", std::string(67, '-').c_str());

    // ── Scene compilation ─────────────────────────────────────────────────────

    bench("compile() — full rebuild", M_SLOW,
        [&] { compile(cs, scene.storage.polytree, scene.descs, {}, view); });

    bench("update_view()", M_FAST,
        [&] { update_view(cs, view); });

    bench("update_compiled_transforms() all dirty", M_FAST,
        [&] { update_compiled_transforms(cs, scene.storage.polytree,
                                         scene.descs, view, {}, true); });

    // ── Dirty-filter variants ─────────────────────────────────────────────────

    {
        std::vector<NodeHandle> dirty;
        dirty.reserve(N_OPAQUE / 10);
        for (uint32_t i = 0; i < N_OPAQUE; i += 10)
            dirty.push_back(scene.opaque[i]);

        bench("update_compiled_transforms() 10% dirty, vc=true",  M_FAST,
            [&] { update_compiled_transforms(cs, scene.storage.polytree,
                                             scene.descs, view, dirty, true); });
        bench("update_compiled_transforms() 10% dirty, vc=false", M_FAST,
            [&] { update_compiled_transforms(cs, scene.storage.polytree,
                                             scene.descs, view, dirty, false); });
    }

    {
        std::vector<NodeHandle> dirty;
        dirty.reserve(N_OPAQUE / 100);
        for (uint32_t i = 0; i < N_OPAQUE; i += 100)
            dirty.push_back(scene.opaque[i]);

        bench("update_compiled_transforms()  1% dirty, vc=true",  M_FAST,
            [&] { update_compiled_transforms(cs, scene.storage.polytree,
                                             scene.descs, view, dirty, true); });
        bench("update_compiled_transforms()  1% dirty, vc=false", M_FAST,
            [&] { update_compiled_transforms(cs, scene.storage.polytree,
                                             scene.descs, view, dirty, false); });
    }

    // ── RenderIR — full visibility (wide frustum) ─────────────────────────────

    build_render_ir(ir_st, cs.scene);

    bench("update_render_ir() — 100% visible", M_FAST,
        [&] { update_render_ir(ir_st, cs.scene); });

    bench("build_render_ir() — full rebuild, 100% visible", M_FAST,
        [&] { build_render_ir(ir_st, cs.scene); });

    // ── RenderFrame ───────────────────────────────────────────────────────────

    bench("build_frame() — full rebuild", M_FAST,
        [&] { build_frame(fr_st, ir_st.ir, cs.scene); });

    bench("update_frame_view() — view sections only", M_FAST,
        [&] { update_frame_view(fr_st, ir_st.ir, cs.scene); });

    // ── Visibility ratio benchmarks ───────────────────────────────────────────
    //
    // 100k opaque objects, k% at z=0 (inside unit-cube frustum), rest at z=100.
    // Unit-cube VP (identity) — all objects at z>0.5 are culled.

    std::printf("\n  Visibility ratio benchmarks  "
                "(100k opaque, unit-cube frustum, identity VP)\n");
    std::printf("  %-55s  %8s  %8s  %8s\n", "Ratio", "build IR", "update IR", "build frame");
    std::printf("  %s\n", std::string(87, '-').c_str());

    struct RatioCase { const char* label; uint32_t n_in; uint32_t n_out; };
    RatioCase cases[] = {
        { "100%  visible (100k in /     0 out)", 100'000,       0 },
        { " 50%  visible ( 50k in / 50k out)",   50'000,  50'000 },
        { " 10%  visible ( 10k in / 90k out)",   10'000,  90'000 },
        { "  1%  visible (  1k in / 99k out)",    1'000,  99'000 },
    };

    for (const auto& c : cases) {
        auto vis = make_visibility_scene(c.n_in, c.n_out);
        const ViewData unit_view = unit_frustum_view();

        CompiledSceneStorage vis_cs{};
        RenderIRStorage      vis_ir{};
        RenderFrameStorage   vis_fr{};

        compile(vis_cs, vis.storage.polytree, vis.descs, {}, unit_view);

        // Warm
        build_render_ir(vis_ir, vis_cs.scene);
        build_frame(vis_fr, vis_ir.ir, vis_cs.scene);

        double build_us = 0, update_us = 0, frame_us = 0;
        {
            auto t0 = std::chrono::high_resolution_clock::now();
            for (uint32_t i = 0; i < M_FAST; ++i)
                build_render_ir(vis_ir, vis_cs.scene);
            auto t1 = std::chrono::high_resolution_clock::now();
            build_us = std::chrono::duration<double, std::micro>(t1 - t0).count() / M_FAST;
        }
        {
            auto t0 = std::chrono::high_resolution_clock::now();
            for (uint32_t i = 0; i < M_FAST; ++i)
                update_render_ir(vis_ir, vis_cs.scene);
            auto t1 = std::chrono::high_resolution_clock::now();
            update_us = std::chrono::duration<double, std::micro>(t1 - t0).count() / M_FAST;
        }
        {
            auto t0 = std::chrono::high_resolution_clock::now();
            for (uint32_t i = 0; i < M_FAST; ++i)
                build_frame(vis_fr, vis_ir.ir, vis_cs.scene);
            auto t1 = std::chrono::high_resolution_clock::now();
            frame_us = std::chrono::duration<double, std::micro>(t1 - t0).count() / M_FAST;
        }

        std::printf("  %-55s  %8.1f  %8.1f  %8.1f\n",
                    c.label, build_us, update_us, frame_us);
    }

    std::printf("\n");
    SUCCEED();
}
