// src/engine/assets/csv_export/csv_export.cpp

#include <engine/assets/csv_export/csv_export.h>
#include <engine/assets/type_extensions.h>

#include <utility>

namespace wz::engine::assets
{
    bool CSVExportData::valid() const noexcept
    {
        return column_count > 0;
    }

    CSVExportTable::CSVExportTable()
    {
        exports_.emplace_back();
        epochs_.push_back(0);
    }

    wz::asset::ResourceHandle CSVExportTable::add(CSVExportData data)
    {
        if (!data.valid())
            return {};

        const uint32_t id = static_cast<uint32_t>(exports_.size());

        exports_.push_back(std::move(data));
        epochs_.push_back(1);

        return wz::asset::ResourceHandle{
            .id    = id,
            .epoch = epochs_[id],
            .type  = kAssetTypeCSVExport,
        };
    }

    const CSVExportData* CSVExportTable::get(
        wz::asset::ResourceHandle handle) const
    {
        if (!handle.valid())
            return nullptr;

        if (handle.type != kAssetTypeCSVExport)
            return nullptr;

        if (handle.id >= exports_.size())
            return nullptr;

        if (epochs_[handle.id] != handle.epoch)
            return nullptr;

        return &exports_[handle.id];
    }

    void CSVExportTable::destroy()
    {
        exports_.clear();
        epochs_.clear();

        exports_.emplace_back();
        epochs_.push_back(0);
    }
}
