#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

#include <engine/collision/collision_frame.h>
#include <engine/assets/engine_asset_library.h>
#include <engine/frame_storage.h>

#include <jobs/dag_scheduler.h>
#include <jobs/frame_execution.h>
#include <jobs/job_graph_template.h>

#include <file/filesystem.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace wz::engine::collision;

    wz::scene::AABB bounds(
        float min_x,
        float min_y,
        float min_z,
        float max_x,
        float max_y,
        float max_z)
    {
        return wz::scene::AABB{
            .min = { min_x, min_y, min_z },
            .max = { max_x, max_y, max_z },
        };
    }

    CollisionWorldEntry entry(
        wz::scene::RuntimeEntityId entity,
        wz::scene::AABB world_bounds,
        uint32_t layer_mask = 1,
        uint32_t collides_with_mask = 0xffffffffu,
        bool enabled = true,
        bool is_trigger = false)
    {
        return CollisionWorldEntry{
            .entity = entity,
            .world_bounds = world_bounds,
            .layer_mask = layer_mask,
            .collides_with_mask = collides_with_mask,
            .is_trigger = is_trigger,
            .enabled = enabled,
        };
    }

    wz::engine::assets::SceneComponentRecord<
        wz::engine::assets::EventListenerComponent>
    listener(
        wz::scene::RuntimeEntityId entity,
        std::vector<std::string> channels)
    {
        return {
            .node = entity,
            .component = {
                .channels = std::move(channels),
            },
        };
    }

    wz::fs::Path test_root(const char* name)
    {
        return wz::fs::join(wz::fs::temp_directory_path(), name);
    }

    void expect_aabb_near(
        const wz::scene::AABB& actual,
        const wz::scene::AABB& expected)
    {
        constexpr float eps = 1e-5f;
        EXPECT_NEAR(actual.min.x, expected.min.x, eps);
        EXPECT_NEAR(actual.min.y, expected.min.y, eps);
        EXPECT_NEAR(actual.min.z, expected.min.z, eps);
        EXPECT_NEAR(actual.max.x, expected.max.x, eps);
        EXPECT_NEAR(actual.max.y, expected.max.y, eps);
        EXPECT_NEAR(actual.max.z, expected.max.z, eps);
    }

    struct CollisionFrameJobData
    {
        wz::engine::FrameStorage* frame = nullptr;
        const wz::engine::assets::SceneInstance* scene = nullptr;
        const wz::engine::assets::CollisionAssetModule* collisions = nullptr;
        bool* ran = nullptr;
    };

    void job_noop(wz::jobs::JobContext&)
    {
    }

    void job_build_collision_frame_for_test(wz::jobs::JobContext& ctx)
    {
        auto* data = static_cast<CollisionFrameJobData*>(ctx.frame_user);
        ASSERT_NE(data, nullptr);
        ASSERT_NE(data->frame, nullptr);
        ASSERT_NE(data->scene, nullptr);
        ASSERT_NE(data->collisions, nullptr);
        ASSERT_NE(data->ran, nullptr);

        build_collision_frame(
            *data->scene,
            *data->collisions,
            data->frame->collision);
        *data->ran = true;
    }
}




























// ─── Adversarial tests ──────────────────────────────────────────────────────





























// ─── Adversarial tests: round 2 ─────────────────────────────────────────────

















#if defined(__clang__)
#pragma clang diagnostic pop
#endif
