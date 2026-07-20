#pragma once

// engine/assets/environment_asset_module.h

#include <asset/system.h>
#include <engine/assets/environment/environment.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    // Authoring recipe for a FrameEnvironment: a name plus references to the
    // frame-global environment pieces. name contributes to the asset key so two
    // environments referencing the same pieces are still distinct assets. Any
    // reference may be left empty — a frame can author any subset, including none.
    struct EnvironmentDesc
    {
        std::string name;
        wz::asset::AssetKey atmosphere{};
        wz::asset::AssetKey ambient_lighting{};
        wz::asset::AssetKey hdri_environment{};
        wz::asset::AssetKey directional_light{};
    };

    // Returned by create_environment(). Wraps the DAG output node key.
    struct EnvironmentAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_environment(). Wraps the ResourceHandle into
    // EnvironmentTable.
    struct EnvironmentHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class EnvironmentAssetModule
    {
    public:
        EnvironmentAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            EnvironmentTable& table);

        // Register a FrameEnvironment in the DAG. Every referenced piece must
        // itself be registered before commit(). Call commit() and resolve_all()
        // on EngineAssetLibrary before querying handles.
        [[nodiscard]] EnvironmentAsset create_environment(
            const EnvironmentDesc& desc);

        [[nodiscard]] EnvironmentHandle get_environment(
            const EnvironmentAsset& asset) const;

        [[nodiscard]] EnvironmentHandle find_environment(
            const EnvironmentAsset& asset) const;

        [[nodiscard]] const EnvironmentData* get_environment_data(
            EnvironmentHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        EnvironmentTable& table_;
    };
}
