// src/engine/assets/diagnostic_table_asset_module.cpp

#include <engine/assets/diagnostic_table_asset_module.h>

#include <engine/assets/key_factories/diagnostic_table.h>
#include <engine/assets/schema_ids.h>
#include <engine/assets/type_extensions.h>

#include <vector>

namespace wz::engine::assets
{
    DiagnosticTableAssetModule::DiagnosticTableAssetModule(
        wz::asset::AssetSystem& system,
        wz::Logger& logger,
        DiagnosticTable& table)
        : system_(system)
        , logger_(logger)
        , table_(table)
    {
    }

    DiagnosticTableAsset DiagnosticTableAssetModule::create_inline_table(
        const InlineDiagnosticTableDesc& desc)
    {
        DiagnosticTableAsset out{};

        if (desc.name.empty()) {
            logger_.error("diagnostic table has empty name");
            return out;
        }

        if (!desc.table.valid()) {
            logger_.error("diagnostic table is invalid: " + desc.name);
            return out;
        }

        const wz::asset::AssetKey key =
            make_inline_diagnostic_table_key(desc.name, desc.table);

        wz::asset::AssetNode node;
        node.key = key;
        node.type = kAssetTypeDiagnosticTable;
        node.schema = kInlineDiagnosticTableSchema;
        node.stage = wz::asset::AssetStage::Source;
        node.payload = std::vector<uint8_t>{};
        node.meta = InlineDiagnosticTableCompileDesc{
            .table = desc.table,
        };

        if (!system_.register_asset(std::move(node), {})) {
            logger_.error("failed to register diagnostic table: " + desc.name);
            return {};
        }

        out.output = key;
        return out;
    }

    DiagnosticTableHandle DiagnosticTableAssetModule::get_table(
        const DiagnosticTableAsset& asset) const
    {
        if (!asset.valid())
            return {};

        DiagnosticTableHandle out{};

        if (const auto* compiled = system_.find_compiled(asset.output))
            out.handle = compiled->handle;

        if (!out.valid())
            logger_.error("diagnostic table handle not found");

        return out;
    }

    const DiagnosticTableData* DiagnosticTableAssetModule::get_table_data(
        DiagnosticTableHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        return table_.get(handle.handle);
    }
}