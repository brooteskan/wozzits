// tests/benchmark_app/benchmark_app_main.cpp
//
// Sample scene: a static grid of 1000 opaque objects, arranged at z=8.
// Press ESC to toggle flying-camera navigation.
// Press BKSPC to quit (only while nav is active).

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include <cassert>
#include <cmath>
#include <vector>

#include <engine/benchmark_app.h>

#include <scene/scene_graph.h>
#include <scene/compile/legacy_classification.h>

// ─── Sample scene builder ─────────────────────────────────────────────────────

static bool build_grid_scene(
    wz::bench::SceneRuntime& runtime,
    wz::engine::AppContext&  ctx,
    uint32_t                 object_count,
    float                    spacing,
    float                    scene_z)
{
    using namespace wz::scene;
    using namespace wz::core::graph;
    using namespace wz::math;

    const uint32_t columns = std::max(
        1u,
        static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(object_count)))));
    const uint32_t rows = (object_count + columns - 1u) / columns;

    SceneBuilder b;

    TransformNode root{};
    root.local = Mat4::identity();
    NodeHandle root_h = add_node(b, root);

    std::vector<NodeHandle> object_nodes;
    object_nodes.reserve(object_count);

    for (uint32_t i = 0; i < object_count; ++i)
    {
        const float x =
            (static_cast<float>(i % columns) - static_cast<float>(columns) * 0.5f) * spacing;
        const float y =
            (static_cast<float>(i / columns) - static_cast<float>(rows)    * 0.5f) * spacing;

        TransformNode node{};
        node.local = Mat4::identity();
        node.local.m[12] = x;
        node.local.m[13] = y;
        node.local.m[14] = scene_z;
        node.flags       = TransformNodeFlag::RenderDomain;
        node.motion_type = TransformNode::MotionType::Static;

        NodeHandle node_h = add_node(b, node);
        add_edge(b, root_h, node_h);
        object_nodes.push_back(node_h);
    }

    auto result = build(std::move(b));
    if (!result.has_value())
        return false;

    runtime.scene = std::move(*result);

    const uint32_t node_cnt = node_count(runtime.scene.polytree);
    runtime.descriptors.resize(node_cnt);

    // Default: no renderable output.
    for (auto& desc : runtime.descriptors)
        desc = RenderableDescriptor{ .node_class = classify_legacy_renderable(RenderPipeline::None) };

    // Object nodes render as opaque geometry (mesh 0, material 0).
    const AABB bounds{
        .min = Vec3{ -0.5f, -0.5f, -0.5f },
        .max = Vec3{  0.5f,  0.5f,  0.5f },
    };
    for (NodeHandle node_h : object_nodes)
    {
        runtime.descriptors[node_h] = RenderableDescriptor{
            .node_class  = classify_legacy_renderable(RenderPipeline::OpaqueGeometry),
            .mesh        = 0,
            .material    = 0,
            .local_bounds = bounds,
            .visible     = true,
        };
    }

    propagate_all(runtime.scene.polytree);
    return true;
}

// ─── Entry point ──────────────────────────────────────────────────────────────

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    constexpr uint32_t kObjectCount = 1000;
    constexpr float    kSpacing     = 1.5f;
    constexpr float    kSceneZ      = 8.0f;

    wz::bench::SceneDefinition scene_def{};
    scene_def.name = "grid-1000-static";
    scene_def.build = [](wz::bench::SceneRuntime& runtime, wz::engine::AppContext& ctx) -> bool {
        return build_grid_scene(runtime, ctx, kObjectCount, kSpacing, kSceneZ);
    };
    // No update callback: static scene, no dirty nodes each frame.

    wz::bench::BenchmarkApp app{};

    if (!wz::bench::init(app, std::move(scene_def)))
    {
        wz::bench::shutdown(app);
        return 1;
    }

    // Position camera 10 units behind the scene origin, looking toward +Z.
    app.camera.x = 0.0f;
    app.camera.y = 0.0f;
    app.camera.z = -10.0f;

    wz::engine::run([&app](wz::engine::Context& ctx, wz::engine::FrameContext& fctx) {
        wz::bench::update(ctx, fctx, app);
        wz::bench::render(app, fctx);
    });

    wz::bench::shutdown(app);
    return 0;
}
