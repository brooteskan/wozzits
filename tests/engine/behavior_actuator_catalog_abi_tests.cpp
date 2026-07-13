// Seam 2a: the device-free behavior-actuator catalog the editor reads to populate
// its statechart `call` picker. This pins the ABI blob's marshaling (the offset/span
// packing that the C# reader decodes) by round-tripping it in-process: build the
// blob, decode byte 0 as WzEditorBehaviorActuatorCatalog, and confirm the builtin
// `move_toward` actuator surfaces with its declared arg schema.

#include <gtest/gtest.h>

#include <engine/abi/wozzits_abi.h>
#include <engine/editor/project_snapshot_abi.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Decode a WzEditorStringSpan against the blob it points into. memcpy-based, so
    // unaligned span offsets are fine (the blob packs strings tightly).
    std::string read_span(
        const std::vector<uint8_t>& blob, const WzEditorStringSpan& span)
    {
        if (span.size == 0u) {
            return {};
        }
        return std::string(
            reinterpret_cast<const char*>(blob.data() + span.offset),
            static_cast<size_t>(span.size));
    }

    template <typename T>
    T read_struct(const std::vector<uint8_t>& blob, uint64_t offset)
    {
        T value{};
        std::memcpy(&value, blob.data() + offset, sizeof(T));
        return value;
    }
}

TEST(BehaviorActuatorCatalogAbi, ExposesMoveTowardWithItsArgSchema)
{
    const std::vector<uint8_t> blob =
        wz::engine::editor::behavior_actuator_catalog_abi_blob();
    ASSERT_GE(blob.size(), sizeof(WzEditorBehaviorActuatorCatalog));

    const auto root =
        read_struct<WzEditorBehaviorActuatorCatalog>(blob, 0);
    EXPECT_EQ(root.ok, 1u);
    EXPECT_EQ(root.abi_version, WZ_ABI_VERSION);
    ASSERT_GE(root.actuators.count, 1u);

    bool found_move_toward = false;
    for (uint64_t i = 0; i < root.actuators.count; ++i) {
        const auto actuator = read_struct<WzEditorBehaviorActuator>(
            blob,
            root.actuators.offset
                + i * sizeof(WzEditorBehaviorActuator));
        if (read_span(blob, actuator.name) != "move_toward") {
            continue;
        }
        found_move_toward = true;

        EXPECT_EQ(read_span(blob, actuator.label), "Move toward");
        ASSERT_EQ(actuator.params.count, 2u);

        const auto target = read_struct<WzEditorBehaviorActuatorParam>(
            blob, actuator.params.offset);
        const auto speed = read_struct<WzEditorBehaviorActuatorParam>(
            blob,
            actuator.params.offset
                + sizeof(WzEditorBehaviorActuatorParam));

        EXPECT_EQ(read_span(blob, target.name), "target");
        EXPECT_EQ(target.kind, 1u);   // WZ_ACTUATOR_PARAM_BINDING

        EXPECT_EQ(read_span(blob, speed.name), "speed");
        EXPECT_EQ(speed.kind, 0u);    // WZ_ACTUATOR_PARAM_SCALAR
        EXPECT_DOUBLE_EQ(speed.default_value, 1.0);
    }
    EXPECT_TRUE(found_move_toward);
}

namespace
{
    bool catalog_has_actuator(
        const std::vector<uint8_t>& blob, std::string_view name)
    {
        const auto root =
            read_struct<WzEditorBehaviorActuatorCatalog>(blob, 0);
        for (uint64_t i = 0; i < root.actuators.count; ++i) {
            const auto a = read_struct<WzEditorBehaviorActuator>(
                blob,
                root.actuators.offset
                    + i * sizeof(WzEditorBehaviorActuator));
            if (read_span(blob, a.name) == name) {
                return true;
            }
        }
        return false;
    }
}

// The project-aware blob loads the project's behavior DLLs so their registered
// actuators surface (that is what makes a project actuator bindable in a chart).
// With no project it is just the built-ins -- and it must never throw on a missing
// or bogus project, only degrade to the built-ins.
TEST(BehaviorActuatorCatalogAbi, ProjectCatalogFallsBackToBuiltinsForEmptyProject)
{
    const std::vector<uint8_t> blob =
        wz::engine::editor::project_behavior_actuator_catalog_abi_blob("", "");
    ASSERT_GE(blob.size(), sizeof(WzEditorBehaviorActuatorCatalog));

    const auto root = read_struct<WzEditorBehaviorActuatorCatalog>(blob, 0);
    EXPECT_EQ(root.ok, 1u);
    EXPECT_EQ(root.abi_version, WZ_ABI_VERSION);
    EXPECT_TRUE(catalog_has_actuator(blob, "move_toward"));
}

TEST(BehaviorActuatorCatalogAbi, ProjectCatalogSurvivesBogusProjectRoot)
{
    // A nonexistent project must not throw -- just the built-ins.
    const std::vector<uint8_t> blob =
        wz::engine::editor::project_behavior_actuator_catalog_abi_blob(
            "Z:/no/such/project/here", "");
    const auto root = read_struct<WzEditorBehaviorActuatorCatalog>(blob, 0);
    EXPECT_EQ(root.ok, 1u);
    EXPECT_TRUE(catalog_has_actuator(blob, "move_toward"));
}
