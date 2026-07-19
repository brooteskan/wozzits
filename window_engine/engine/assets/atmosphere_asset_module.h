#pragma once

// engine/assets/atmosphere_asset_module.h

#include <asset/system.h>
#include <engine/assets/atmosphere/atmosphere.h>

#include <logging/logger.h>

#include <string>

namespace wz::engine::assets
{
    // Authoring recipe for an Atmosphere: the global fog dials. name contributes
    // to the asset key so differently-named atmospheres with identical dials are
    // distinct assets. Defaults mirror the compiler's declared parameter
    // defaults — a pale haze that is switched off.
    struct AtmosphereDesc
    {
        std::string name;
        float fog_color[3]{ 0.6f, 0.7f, 0.8f };  // linear RGB at full density
        float fog_density = 0.0f;                // thickness per metre
        float fog_start_distance = 0.0f;         // metres before fog accumulates
        float fog_height_falloff = 0.0f;         // rate the fog thins with height
        bool  fog_enabled = false;               // master switch
    };

    // Returned by create_atmosphere(). Wraps the DAG output node key.
    struct AtmosphereAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    // Returned by get_atmosphere(). Wraps the ResourceHandle into
    // AtmosphereTable.
    struct AtmosphereHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class AtmosphereAssetModule
    {
    public:
        AtmosphereAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            AtmosphereTable& table);

        // Register an Atmosphere in the DAG. Call commit() and resolve_all() on
        // EngineAssetLibrary before querying handles.
        [[nodiscard]] AtmosphereAsset create_atmosphere(
            const AtmosphereDesc& desc);

        [[nodiscard]] AtmosphereHandle get_atmosphere(
            const AtmosphereAsset& asset) const;

        [[nodiscard]] AtmosphereHandle find_atmosphere(
            const AtmosphereAsset& asset) const;

        [[nodiscard]] const AtmosphereData* get_atmosphere_data(
            AtmosphereHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        AtmosphereTable& table_;
    };
}
