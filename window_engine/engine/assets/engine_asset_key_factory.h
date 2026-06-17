#pragma once

#include <asset/draft.h>

namespace wz::engine::assets
{
    [[nodiscard]] bool engine_key_factory_handles(
        wz::asset::SchemaID schema,
        wz::asset::AssetType type) noexcept;

    [[nodiscard]] wz::asset::AssetKeyFactoryFn make_engine_asset_key_factory(
        const wz::asset::CompilerRegistry& registry);
}
