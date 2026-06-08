#pragma once

// engine/assets/mesh_derived_field_asset_module.h

#include <asset/system.h>
#include <engine/assets/mesh_asset_module.h>
#include <engine/assets/mesh_derived_field/mesh_derived_field.h>

#include <string>
#include <vector>

namespace wz::engine::assets
{
    struct ExplicitMeshDerivedFieldDesc
    {
        std::string name;
        MeshAsset source_mesh;
        MeshDerivedFieldDomain domain = MeshDerivedFieldDomain::Vertex;
        uint32_t element_count = 0;
        std::vector<MeshDerivedFieldChannelDesc> channels;
    };

    struct MeshDerivedFieldAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct MeshDerivedFieldHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class MeshDerivedFieldAssetModule
    {
    public:
        explicit MeshDerivedFieldAssetModule(
            wz::asset::AssetSystem& system,
            MeshDerivedFieldTable& table);

        [[nodiscard]] MeshDerivedFieldAsset create_explicit_field(
            const ExplicitMeshDerivedFieldDesc& desc);

        [[nodiscard]] MeshDerivedFieldHandle get_mesh_derived_field(
            const MeshDerivedFieldAsset& asset) const;

        [[nodiscard]] const MeshDerivedFieldData* get_mesh_derived_field_data(
            MeshDerivedFieldHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        MeshDerivedFieldTable& table_;
    };
}
