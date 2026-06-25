#include <gtest/gtest.h>

#include <engine/editor/asset_graph_schema_registry.h>

#include <asset/compiler.h>
#include <asset/types.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <algorithm>

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

    // Authorable mesh nodes are renamable in the editor: each mesh compiler
    // declares a "name" string param, which the asset-graph snapshot's
    // display_name honors above the schema-label fallback.
    TEST(AssetGraphSchemaRegistry, AuthorableMeshCompilersDeclareNameParam)
    {
        namespace ea = wz::engine::assets;
        const wz::asset::CompilerRegistry registry =
            wz::engine::editor::build_asset_graph_schema_registry();

        const wz::asset::SchemaID mesh_schemas[] = {
            ea::kProceduralTriangleMeshSchema,
            ea::kProceduralQuadMeshSchema,
            ea::kProceduralCubeMeshSchema,
            ea::kProceduralClipmapLatticeMeshSchema,
            ea::kGLBMeshSchema,
        };

        for (const wz::asset::SchemaID schema : mesh_schemas) {
            const wz::asset::AssetCompiler* compiler =
                registry.find(schema, ea::kAssetTypeMesh);
            ASSERT_NE(compiler, nullptr)
                << "no mesh compiler for schema " << schema.value;

            const bool has_name = std::ranges::any_of(
                compiler->parameters,
                [](const wz::asset::ParamDecl& p)
                {
                    return p.name == "name"
                        && p.type == wz::asset::ParamType::String;
                });
            EXPECT_TRUE(has_name)
                << "mesh schema " << schema.value << " missing a 'name' param";
        }
    }
}
