#include <gtest/gtest.h>

#include <engine/editor/asset_graph_schema_registry.h>

#include <asset/compiler.h>
#include <asset/types.h>

namespace
{
    // The builder must run with no GPU device and produce a registry that
    // carries the engine's REAL declared compiler schemas — not the lossy,
    // edge-inferred shim the in-process ABI used before (issue #187).
    TEST(AssetGraphSchemaRegistry, BuildsDeviceFreeSchemaOnlyRegistry)
    {
        const wz::asset::CompilerRegistry registry =
            wz::engine::editor::build_asset_graph_schema_registry();

        // Real engine compilers are present (not an empty / draft-derived set).
        EXPECT_GT(registry.size(), 0u);

        bool any_named_typed_port = false;
        for (const auto& [key, compiler] : registry.compilers()) {
            (void)key;

            // Schema-only: the compile fns are stripped, so none dangle onto the
            // throwaway stubs the builder used.
            EXPECT_FALSE(static_cast<bool>(compiler.compile));

            for (const wz::asset::InputPort& port : compiler.input_ports) {
                if (!port.name.empty()
                    && port.type != wz::asset::AssetType::Unknown)
                {
                    any_named_typed_port = true;
                }
            }
        }

        // The old draft-inference shim could only ever emit unnamed,
        // edge-derived ports. A declared, named, typed input port proves the
        // registry carries the real schemas.
        EXPECT_TRUE(any_named_typed_port);
    }
}
