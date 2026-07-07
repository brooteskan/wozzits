#pragma once

// engine/assets/sky_gaussian/sky_gaussian_serialize.h
//
// JSON (de)serialization for a SkyGaussianSet using the in-repo wz::json tree
// (external/json). Schema v1 (Seam A, #260):
//   {
//     "version": 1,
//     "source_name": "...", "source_width": W, "source_height": H,
//     "lobes": [ { "direction":[x,y,z], "sharpness":f, "amplitude":[r,g,b] } ],
//     "point_sources": [
//        { "direction":[x,y,z], "radiance":[r,g,b], "solid_angle":f } ]
//   }

#include <engine/assets/sky_gaussian/sky_gaussian.h>

#include <external/json/json_document.h>

#include <string>

namespace wz::engine::assets::sky
{
    wz::json::JSONValue sky_gaussian_to_json(const SkyGaussianSet& set);

    bool sky_gaussian_from_json(
        const wz::json::JSONValue& value,
        SkyGaussianSet& out,
        std::string& error);

} // namespace wz::engine::assets::sky
