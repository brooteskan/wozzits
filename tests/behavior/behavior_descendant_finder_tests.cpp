#include "behavior_test_support.h"

#include <engine/behavior/behavior_plugin_adapter.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// wz_find_descendant_by_name: a subtree-scoped, instance-safe name lookup. The
// motivating case is a spawned prefab (an enemy tank) resolving ITS OWN grafted
// GLB "turret" child, when every spawned tank carries an identically-named
// turret. A global find_entity_by_name cannot tell the instances apart; scoping
// the search to the caller's own subtree can.
namespace
{
    struct SeekResult
    {
        WzBehaviorEntityId self = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId turret = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId by_self_name = WZ_INVALID_BEHAVIOR_ENTITY;
        WzBehaviorEntityId missing = WZ_INVALID_BEHAVIOR_ENTITY;
    };

    struct DescendantProbe
    {
        std::vector<SeekResult> results;
    };

    DescendantProbe* g_descendant_probe = nullptr;

    // Runs in on_init (where an agent would resolve + cache its turret handle).
    void on_turret_seeker_init(
        const WzBehaviorInitFacts* facts,
        WzBehaviorEntityId self,
        void*)
    {
        SeekResult r{};
        r.self = self;
        wz_find_descendant_by_name(facts, self, "turret", &r.turret);
        // "tank" is the ROOT's own name: must NOT resolve (strict descendant),
        // and the OTHER tank's "tank" root is outside this subtree.
        wz_find_descendant_by_name(facts, self, "tank", &r.by_self_name);
        wz_find_descendant_by_name(facts, self, "nonexistent", &r.missing);
        if (g_descendant_probe) {
            g_descendant_probe->results.push_back(r);
        }
    }

    void on_turret_seeker_event(
        const WzBehaviorFrameFacts*,
        const WzBehaviorEvent*,
        void*)
    {
    }

    uint8_t register_turret_seeker_pack(WzBehaviorPluginApi* api)
    {
        static const char* events[] = { "frame.update" };
        const WzBehaviorModuleDesc desc{
            .size = sizeof(WzBehaviorModuleDesc),
            .module = "turret_seeker",
            .on_event = on_turret_seeker_event,
            .on_init = on_turret_seeker_init,
            .event_channels = events,
            .event_channel_count = 1u,
        };
        return api && api->version == WZ_BEHAVIOR_ABI_VERSION
            && api->register_module_desc
            ? api->register_module_desc(api->user, &desc)
            : 0u;
    }

    // Author a tank: root (carries the seeker behavior) -> body -> turret, the
    // same body/turret/gun nesting a grafted tank GLB produces.
    void add_tank(
        wz::engine::assets::SceneAssetData& asset,
        const std::string& prefix)
    {
        wz::engine::assets::SceneNodeAsset root{};
        root.id = prefix;
        root.name = "tank";
        root.behavior = wz::engine::assets::SceneBehaviorAsset{
            .id = prefix + ".seeker",
            .module = "turret_seeker",
        };

        wz::engine::assets::SceneNodeAsset body{};
        body.id = prefix + "/body";
        body.parent_id = prefix;
        body.name = "body";

        wz::engine::assets::SceneNodeAsset turret{};
        turret.id = prefix + "/turret";
        turret.parent_id = prefix + "/body";
        turret.name = "turret";

        asset.nodes.push_back(std::move(root));
        asset.nodes.push_back(std::move(body));
        asset.nodes.push_back(std::move(turret));
    }
}

TEST(BehaviorDescendantFinder, ResolvesOwnTurretPerInstance)
{
    wz::engine::assets::SceneAssetData asset{};
    asset.name = "descendant_finder_two_tanks";
    add_tank(asset, "tankA");
    add_tank(asset, "tankB");

    auto result = wz::engine::assets::instantiate_scene(asset);
    ASSERT_TRUE(result.ok()) << result.error_detail;
    auto& scene = result.instance;

    BehaviorRegistry registry;
    BehaviorPluginHost plugins;
    DescendantProbe probe{};
    g_descendant_probe = &probe;
    ASSERT_TRUE(plugins.register_static_pack(
        registry,
        register_turret_seeker_pack));

    initialize_behaviors(scene, registry);

    ASSERT_EQ(probe.results.size(), 2u);

    const auto a_root = scene.authored_to_runtime["tankA"];
    const auto a_turret = scene.authored_to_runtime["tankA/turret"];
    const auto b_root = scene.authored_to_runtime["tankB"];
    const auto b_turret = scene.authored_to_runtime["tankB/turret"];

    for (const SeekResult& r : probe.results) {
        // Each tank resolved ITS OWN turret -- the whole point of subtree scoping.
        if (r.self == a_root) {
            EXPECT_EQ(r.turret, a_turret);
        }
        else if (r.self == b_root) {
            EXPECT_EQ(r.turret, b_turret);
        }
        else {
            ADD_FAILURE() << "unexpected seeker entity " << r.self;
        }

        // "tank" is the caller's own name and the other tank's root name; neither
        // is a strict descendant of the caller, so the lookup must miss.
        EXPECT_EQ(r.by_self_name, WZ_INVALID_BEHAVIOR_ENTITY);
        // A name nobody carries also misses.
        EXPECT_EQ(r.missing, WZ_INVALID_BEHAVIOR_ENTITY);
    }

    g_descendant_probe = nullptr;
}
