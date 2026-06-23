#include <engine/editor/asset_catalog.h>

#include <engine/assets/type_extensions.h>
#include <engine/editor/asset_graph_schema_registry.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <unordered_map>

namespace wz::engine::editor
{
    namespace
    {
        namespace ea = wz::engine::assets;

        // Category bucket from the AssetType numeric range, mirroring the
        // engine's documented type-range allocation (type_extensions.h).
        std::string category_name(wz::asset::AssetType type)
        {
            const uint16_t value = static_cast<uint16_t>(type);
            if (value <= 63u) {
                return "Generic";
            }
            if (value <= 127u) {
                return "Foundation";
            }
            if (value <= 191u) {
                return "CPU data";
            }
            if (value >= 512u && value <= 1023u) {
                return "GPU resident";
            }
            if (value >= 1024u && value <= 2047u) {
                return "Shader and pipeline";
            }
            if (value >= 2048u && value <= 4095u) {
                return "Scene and content";
            }
            if (value >= 4096u && value <= 8191u) {
                return "Tooling";
            }
            return "Extension";
        }

        std::string type_label(wz::asset::AssetType type)
        {
            const std::string_view name =
                ea::asset_type_display_name_view(type);
            if (!name.empty()) {
                return std::string(name);
            }
            return "type "
                + std::to_string(static_cast<uint32_t>(
                    static_cast<uint16_t>(type)));
        }

        std::string schema_label(wz::asset::SchemaID schema)
        {
            const std::string_view name =
                ea::schema_display_name_view(schema);
            if (!name.empty()) {
                return std::string(name);
            }
            char buffer[24]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "schema %08x",
                static_cast<uint32_t>(schema.value & 0xffffffffull));
            return std::string(buffer);
        }
    }

    bool asset_type_needs_rhi_migration(wz::asset::AssetType type)
    {
        // Output types whose GPU residency still lives on the legacy
        // gpu_resident_* tables (issue #186): fields, sparse operators, sparse
        // meshes, cluster hierarchies, and the GPU textures the rhi backend
        // cannot create yet. Everything else either has no GPU residency or is
        // already realized through the rhi path.
        //
        // Scalar field is OFF this list (#197): its residency is now published
        // onto the wozzits-rhi GpuResourceRegistry as an R32F texture at compile
        // time, so it qualifies as migrated and is authorable in the browser.
        // (Its rhi *render* path is still pending in #195; until then it draws
        // through the legacy path.)
        return type == ea::kAssetTypeVectorField
            || type == ea::kAssetTypeMeshDerivedField
            || type == ea::kAssetTypeMeshSparseOperator
            || type == ea::kAssetTypeGpuSparseMesh
            || type == ea::kAssetTypeMeshClusterHierarchy
            || type == ea::kAssetTypeTexture
            || type == ea::kAssetTypeEnvironmentMap;
    }

    std::vector<AssetCatalogEntry> build_asset_catalog()
    {
        const wz::asset::CompilerRegistry registry =
            build_asset_graph_schema_registry();

        std::unordered_map<uint16_t, AssetCatalogEntry> by_type;
        for (const auto& [key, compiler] : registry.compilers()) {
            (void)compiler;
            if (asset_type_needs_rhi_migration(key.type)) {
                continue;
            }

            const uint16_t type_value = static_cast<uint16_t>(key.type);
            auto [it, inserted] = by_type.try_emplace(type_value);
            AssetCatalogEntry& entry = it->second;
            if (inserted) {
                entry.type = key.type;
                entry.type_name = type_label(key.type);
                entry.category = category_name(key.type);
            }
            entry.schemas.push_back(AssetCatalogSchema{
                .schema = key.schema,
                .label = schema_label(key.schema),
            });
        }

        std::vector<AssetCatalogEntry> out;
        out.reserve(by_type.size());
        for (auto& [type_value, entry] : by_type) {
            (void)type_value;
            std::sort(
                entry.schemas.begin(),
                entry.schemas.end(),
                [](const AssetCatalogSchema& a, const AssetCatalogSchema& b)
                {
                    if (a.label == b.label) {
                        return a.schema.value < b.schema.value;
                    }
                    return a.label < b.label;
                });
            out.push_back(std::move(entry));
        }

        std::sort(
            out.begin(),
            out.end(),
            [](const AssetCatalogEntry& a, const AssetCatalogEntry& b)
            {
                if (a.category != b.category) {
                    return a.category < b.category;
                }
                if (a.type_name != b.type_name) {
                    return a.type_name < b.type_name;
                }
                return static_cast<uint16_t>(a.type)
                    < static_cast<uint16_t>(b.type);
            });
        return out;
    }
}
