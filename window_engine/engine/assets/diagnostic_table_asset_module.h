#pragma once

// engine/assets/diagnostic_table_asset_module.h

#include <asset/system.h>
#include <engine/assets/diagnostics/diagnostic_table.h>
#include <logging/logger.h>

namespace wz::engine::assets
{
    struct DiagnosticTableAsset
    {
        wz::asset::AssetKey output{};

        bool valid() const noexcept
        {
            return !(output == wz::asset::AssetKey{});
        }
    };

    struct DiagnosticTableHandle
    {
        wz::asset::ResourceHandle handle{};

        bool valid() const noexcept
        {
            return handle.valid();
        }
    };

    class DiagnosticTableAssetModule
    {
    public:
        DiagnosticTableAssetModule(
            wz::asset::AssetSystem& system,
            wz::Logger& logger,
            DiagnosticTable& table);

        DiagnosticTableAsset create_inline_table(
            const InlineDiagnosticTableDesc& desc);

        DiagnosticTableHandle get_table(
            const DiagnosticTableAsset& asset) const;

        const DiagnosticTableData* get_table_data(
            DiagnosticTableHandle handle) const;

    private:
        wz::asset::AssetSystem& system_;
        wz::Logger& logger_;
        DiagnosticTable& table_;
    };
}