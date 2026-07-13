// S1: the device-free behavior-MODULE param catalog the editor reads to render typed,
// discoverable config fields for a behavior (instead of read-only text). This pins the
// ABI blob's marshaling (the offset/span packing the C# reader decodes) by round-
// tripping it in-process: build the blob, decode byte 0 as WzEditorBehaviorModuleCatalog,
// and confirm builtin modules surface with their declared param schema (key/type/default).

#include <gtest/gtest.h>

#include <engine/abi/wozzits_abi.h>
#include <engine/editor/project_snapshot_abi.h>

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
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

    std::optional<WzEditorBehaviorModule> find_module(
        const std::vector<uint8_t>& blob, std::string_view name)
    {
        const auto root = read_struct<WzEditorBehaviorModuleCatalog>(blob, 0);
        for (uint64_t i = 0; i < root.modules.count; ++i) {
            const auto m = read_struct<WzEditorBehaviorModule>(
                blob,
                root.modules.offset + i * sizeof(WzEditorBehaviorModule));
            if (read_span(blob, m.module) == name) {
                return m;
            }
        }
        return std::nullopt;
    }

    std::optional<WzEditorBehaviorModuleParam> find_param(
        const std::vector<uint8_t>& blob,
        const WzEditorBehaviorModule& module,
        std::string_view key)
    {
        for (uint64_t i = 0; i < module.params.count; ++i) {
            const auto p = read_struct<WzEditorBehaviorModuleParam>(
                blob,
                module.params.offset
                    + i * sizeof(WzEditorBehaviorModuleParam));
            if (read_span(blob, p.key) == key) {
                return p;
            }
        }
        return std::nullopt;
    }
}

TEST(BehaviorModuleCatalogAbi, ExposesBuiltinModulesWithDeclaredParams)
{
    const std::vector<uint8_t> blob =
        wz::engine::editor::behavior_module_catalog_abi_blob();
    ASSERT_GE(blob.size(), sizeof(WzEditorBehaviorModuleCatalog));

    const auto root = read_struct<WzEditorBehaviorModuleCatalog>(blob, 0);
    EXPECT_EQ(root.ok, 1u);
    EXPECT_EQ(root.abi_version, WZ_ABI_VERSION);
    ASSERT_GE(root.modules.count, 1u);

    // statechart_runner declares chart / chart_ir as STRING params -- pins the STRING
    // path (type + the default_string span the FLOAT path leaves empty).
    const auto runner = find_module(blob, "statechart_runner");
    ASSERT_TRUE(runner.has_value());
    const auto chart_ir = find_param(blob, *runner, "chart_ir");
    ASSERT_TRUE(chart_ir.has_value());
    EXPECT_EQ(chart_ir->type, 3u);   // WZ_BEHAVIOR_PARAM_STRING
    EXPECT_EQ(read_span(blob, chart_ir->label), "Chart IR (JSON)");

    // quantum_agent declares a FLOAT "decisions" param (authoring default 2) -- pins the
    // FLOAT path incl. default_number.
    const auto agent = find_module(blob, "quantum_agent");
    ASSERT_TRUE(agent.has_value());
    const auto decisions = find_param(blob, *agent, "decisions");
    ASSERT_TRUE(decisions.has_value());
    EXPECT_EQ(decisions->type, 1u);   // WZ_BEHAVIOR_PARAM_FLOAT
    EXPECT_DOUBLE_EQ(decisions->default_number, 2.0);
}

// The project-aware blob loads the project's behavior DLLs so THEIR modules' declared
// params surface too. With no (or a bogus) project it must never throw -- just degrade
// to the built-ins.
TEST(BehaviorModuleCatalogAbi, ProjectCatalogFallsBackToBuiltinsForEmptyProject)
{
    const std::vector<uint8_t> blob =
        wz::engine::editor::project_behavior_module_catalog_abi_blob("", "");
    const auto root = read_struct<WzEditorBehaviorModuleCatalog>(blob, 0);
    EXPECT_EQ(root.ok, 1u);
    EXPECT_EQ(root.abi_version, WZ_ABI_VERSION);
    EXPECT_TRUE(find_module(blob, "quantum_agent").has_value());
}

TEST(BehaviorModuleCatalogAbi, ProjectCatalogSurvivesBogusProjectRoot)
{
    const std::vector<uint8_t> blob =
        wz::engine::editor::project_behavior_module_catalog_abi_blob(
            "Z:/no/such/project/here", "");
    const auto root = read_struct<WzEditorBehaviorModuleCatalog>(blob, 0);
    EXPECT_EQ(root.ok, 1u);
    EXPECT_TRUE(find_module(blob, "quantum_agent").has_value());
}
