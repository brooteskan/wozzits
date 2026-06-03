#include "collision_frame_test_support.h"

TEST(CollisionFrameJobIntegration, BoundSceneBuildsFrameThroughScheduler)
{
    using namespace wz::engine::assets;

    const wz::fs::Path root = test_root("wz_collision_job_integration");
    ASSERT_EQ(wz::fs::create_directories(root), wz::fs::FileError::None);

    wz::Logger logger;
    wz::gpu::Device device{};
    EngineAssetLibrary assets{ device, logger, root };

    const auto mesh = assets.meshes().create_procedural_mesh({
        .name = "collision/source_cube_job_integration",
        .kind = ProceduralMeshKind::Cube,
    });
    ASSERT_TRUE(mesh.valid());

    const auto collision = assets.collisions().create_from_mesh({
        .name = "collision/cube_job_integration",
        .mesh = mesh,
        .build_method = CollisionBuildMethod::Bounds,
    });
    ASSERT_TRUE(collision.valid());

    ASSERT_TRUE(assets.commit());
    ASSERT_TRUE(assets.resolve_all().ok());

    SceneAssetData scene{};
    scene.name = "collision_job_integration";

    SceneNodeAsset a{};
    a.id = "a";
    a.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene.nodes.push_back(std::move(a));

    SceneNodeAsset b{};
    b.id = "b";
    b.local.translation[0] = 0.25f;
    b.collision = SceneCollisionAsset{
        .collision_asset = collision.output,
    };
    scene.nodes.push_back(std::move(b));

    auto result = instantiate_scene(scene);
    ASSERT_TRUE(result.ok()) << result.error_detail;

    wz::engine::FrameStorage frame{};
    bool collision_job_ran = false;
    CollisionFrameJobData collision_data{
        .frame = &frame,
        .scene = &result.instance,
        .collisions = &assets.collisions(),
        .ran = &collision_job_ran,
    };

    wz::jobs::JobGraphTemplate graph;
    const auto compile_scene = graph.add_job({
        .name = "compile_scene",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_noop,
    });
    const auto build_collision_frame = graph.add_job({
        .name = "build_collision_frame",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_build_collision_frame_for_test,
    });
    const auto build_render_ir = graph.add_job({
        .name = "build_render_ir",
        .lane = wz::jobs::ExecutionLane::MainThread,
        .run = job_noop,
    });

    ASSERT_TRUE(graph.add_dependency(compile_scene, build_collision_frame));
    ASSERT_TRUE(graph.add_dependency(build_collision_frame, build_render_ir));
    ASSERT_TRUE(graph.commit());

    wz::jobs::FrameExecution exec;
    exec.reset(graph);
    exec.bind(compile_scene, nullptr);
    exec.bind(build_collision_frame, &collision_data);
    exec.bind(build_render_ir, nullptr);

    wz::jobs::DagScheduler scheduler;
    scheduler.execute(graph, exec);

    EXPECT_TRUE(collision_job_ran);
    EXPECT_EQ(exec.remaining_jobs, 0u);
    EXPECT_EQ(exec.status[build_collision_frame], wz::jobs::JobStatus::Done);

    ASSERT_EQ(frame.collision.world.size(), 2u);
    ASSERT_EQ(frame.collision.events.size(), 1u);
    EXPECT_EQ(frame.collision.events[0].kind, CollisionEventKind::Enter);
}

