#include <gtest/gtest.h>

// A3-T1 (issue #77 / umbrella #320): the scene fingerprint's contract is
// COMPLETENESS over authored data -- two scenes that differ anywhere an author
// can reach must not hash the same. Nothing keys a cache on the hash today, so
// a miss costs only a wrong log line; the moment something does, a miss becomes
// a stale scene nobody rebuilds. This table is what keeps the contract honest,
// and it is where a newly authored component earns its line.
//
// The A3 visit measured four authored differences the fingerprint could not
// see -- render_order, active, render_to_texture (presence AND target), and
// motion_filter -- plus mesh_index, mesh_sparse_operator_source,
// mesh_level_mask_source and scene-level sky_draws. Each has a case below.

#include <engine/assets/scene/scene_asset_data.h>
#include <engine/assets/scene/scene_fingerprint.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace
{
    using namespace wz::engine::assets;

    SceneAssetData base_scene()
    {
        SceneAssetData scene;
        scene.name = "fingerprint_sensitivity";
        SceneNodeAsset node;
        node.id = "root";
        node.name = "root";
        scene.nodes.push_back(std::move(node));
        return scene;
    }

    // Each mutation must move the hash. Named so a failure says which authored
    // member went invisible rather than just "hashes matched".
    struct Mutation
    {
        const char* what;
        std::function<void(SceneAssetData&)> apply;
    };

    const std::vector<Mutation>& mutations()
    {
        static const std::vector<Mutation> table = {
            // --- core node fields ---
            { "id",            [](SceneAssetData& s) { s.nodes[0].id = "other"; } },
            { "name",          [](SceneAssetData& s) { s.nodes[0].name = "other"; } },
            { "visible",       [](SceneAssetData& s) { s.nodes[0].visible = false; } },
            { "active",        [](SceneAssetData& s) { s.nodes[0].active = false; } },
            { "render_order",  [](SceneAssetData& s) { s.nodes[0].render_order = 7; } },
            { "translation",
              [](SceneAssetData& s) { s.nodes[0].local.translation[1] = 3.0f; } },
            { "rotation_quat",
              [](SceneAssetData& s) { s.nodes[0].local.rotation_quat[0] = 0.5f; } },
            { "scale",
              [](SceneAssetData& s) { s.nodes[0].local.scale[2] = 2.0f; } },
            { "parent_id",
              [](SceneAssetData& s) { s.nodes[0].parent_id = "somebody"; } },

            // --- components the A3 visit measured as invisible ---
            { "render_to_texture presence",
              [](SceneAssetData& s) {
                  s.nodes[0].render_to_texture = SceneRenderToTextureAsset{};
              } },
            { "render_to_texture.target_node_id",
              [](SceneAssetData& s) {
                  SceneRenderToTextureAsset rtt;
                  rtt.target_node_id = 9u;
                  s.nodes[0].render_to_texture = rtt;
              } },
            { "render_to_texture.include_descendants",
              [](SceneAssetData& s) {
                  SceneRenderToTextureAsset rtt;
                  rtt.include_descendants = false;
                  s.nodes[0].render_to_texture = rtt;
              } },
            { "motion_filter presence",
              [](SceneAssetData& s) {
                  s.nodes[0].motion_filter = SceneMotionFilterAsset{};
              } },
            { "motion_filter.translation_smoothing",
              [](SceneAssetData& s) {
                  SceneMotionFilterAsset filter;
                  filter.translation_smoothing[0] = 0.25f;
                  s.nodes[0].motion_filter = filter;
              } },
            { "motion_filter.pitch.limit_max_degrees",
              [](SceneAssetData& s) {
                  SceneMotionFilterAsset filter;
                  filter.pitch.limit = true;
                  filter.pitch.limit_max_degrees = 80.0f;
                  s.nodes[0].motion_filter = filter;
              } },
            { "mesh_index",
              [](SceneAssetData& s) { s.nodes[0].mesh_index = 4u; } },
            { "mesh_sparse_operator_source presence",
              [](SceneAssetData& s) {
                  s.nodes[0].mesh_sparse_operator_source =
                      SceneMeshSparseOperatorSourceAsset{};
              } },
            { "mesh_sparse_operator_source.operator_id",
              [](SceneAssetData& s) {
                  SceneMeshSparseOperatorSourceAsset source;
                  source.operator_id = "cotangent";
                  s.nodes[0].mesh_sparse_operator_source = source;
              } },
            { "mesh_level_mask_source presence",
              [](SceneAssetData& s) {
                  s.nodes[0].mesh_level_mask_source =
                      SceneMeshLevelMaskSourceAsset{};
              } },
            { "mesh_level_mask_source.regions[0].max_value",
              [](SceneAssetData& s) {
                  SceneMeshLevelMaskSourceAsset source;
                  source.regions[0].max_value = 0.5f;
                  s.nodes[0].mesh_level_mask_source = source;
              } },

            // A3-C1-F1 (#77 visit 2): mesh_processing's source/processed/hierarchy
            // AssetKeys are authored and written to disk but were absent from the
            // mix, so two mesh_processing components differing only in one of them
            // collided. Presence + each key are separate mutations here, so the
            // pairwise-distinctness pass fails if any of the three stops mixing.
            { "mesh_processing presence",
              [](SceneAssetData& s) {
                  s.nodes[0].mesh_processing = SceneMeshProcessingAsset{};
              } },
            { "mesh_processing.source_mesh_asset",
              [](SceneAssetData& s) {
                  SceneMeshProcessingAsset p;
                  p.source_mesh_asset.content_hash.lo = 0x1234;
                  s.nodes[0].mesh_processing = p;
              } },
            { "mesh_processing.processed_mesh_asset",
              [](SceneAssetData& s) {
                  SceneMeshProcessingAsset p;
                  p.processed_mesh_asset.content_hash.lo = 0x1234;
                  s.nodes[0].mesh_processing = p;
              } },
            { "mesh_processing.hierarchy_asset",
              [](SceneAssetData& s) {
                  SceneMeshProcessingAsset p;
                  p.hierarchy_asset.content_hash.lo = 0x1234;
                  s.nodes[0].mesh_processing = p;
              } },

            // --- scene level ---
            { "sky_draws presence",
              [](SceneAssetData& s) {
                  s.sky_draws.push_back(SceneSkyDrawAsset{});
              } },
            { "sky_draws[0].radius",
              [](SceneAssetData& s) {
                  SceneSkyDrawAsset sky;
                  sky.radius = 500.0f;
                  s.sky_draws.push_back(sky);
              } },
            { "scene name",
              [](SceneAssetData& s) { s.name = "renamed"; } },
            { "node count",
              [](SceneAssetData& s) {
                  SceneNodeAsset extra;
                  extra.id = "second";
                  s.nodes.push_back(std::move(extra));
              } },
        };
        return table;
    }

    TEST(SceneFingerprintSensitivity, EveryAuthoredMutationMovesTheHash)
    {
        const SceneAssetData base = base_scene();
        const uint64_t base_hash = scene_asset_fingerprint(base);

        for (const Mutation& mutation : mutations()) {
            SceneAssetData mutated = base;
            mutation.apply(mutated);
            EXPECT_NE(scene_asset_fingerprint(mutated), base_hash)
                << "authored change is INVISIBLE to the fingerprint: "
                << mutation.what;
        }
    }

    // Distinctness, not just "differs from base": two mutations that collide with
    // each other would each pass the test above while still making two different
    // scenes indistinguishable.
    TEST(SceneFingerprintSensitivity, MutationsDoNotCollideWithEachOther)
    {
        const SceneAssetData base = base_scene();
        const std::vector<Mutation>& table = mutations();

        std::vector<uint64_t> hashes;
        hashes.reserve(table.size());
        for (const Mutation& mutation : table) {
            SceneAssetData mutated = base;
            mutation.apply(mutated);
            hashes.push_back(scene_asset_fingerprint(mutated));
        }

        for (std::size_t i = 0; i < hashes.size(); ++i) {
            for (std::size_t j = i + 1; j < hashes.size(); ++j) {
                EXPECT_NE(hashes[i], hashes[j])
                    << "two different authored scenes hash the same: '"
                    << table[i].what << "' vs '" << table[j].what << "'";
            }
        }
    }

    // The fingerprint is authored identity, so it must be a pure function of the
    // authored data -- same content, same hash, independent of how it was built.
    TEST(SceneFingerprintSensitivity, IdenticalContentHashesIdentically)
    {
        const SceneAssetData a = base_scene();
        SceneAssetData b;
        b.name = a.name;
        SceneNodeAsset node;
        node.id = "root";
        node.name = "root";
        b.nodes.push_back(std::move(node));

        EXPECT_EQ(scene_asset_fingerprint(a), scene_asset_fingerprint(b));
    }

    // Node ORDER is fingerprint-significant by design: the renderer walks the
    // flat array in order, so a reorder is a different authored scene. Pinned
    // because it is a decision, not an accident, and the obvious "canonicalize by
    // id before hashing" refactor would silently reverse it.
    TEST(SceneFingerprintSensitivity, NodeOrderIsSignificant)
    {
        SceneAssetData ab;
        ab.name = "order";
        SceneNodeAsset a;
        a.id = "a";
        SceneNodeAsset b;
        b.id = "b";
        ab.nodes.push_back(a);
        ab.nodes.push_back(b);

        SceneAssetData ba;
        ba.name = "order";
        ba.nodes.push_back(b);
        ba.nodes.push_back(a);

        EXPECT_NE(scene_asset_fingerprint(ab), scene_asset_fingerprint(ba));
    }
}
