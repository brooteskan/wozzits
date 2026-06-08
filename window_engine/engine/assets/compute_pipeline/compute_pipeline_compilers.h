#pragma once

// engine/assets/compute_pipeline/compute_pipeline_compilers.h

#include <asset/compiler.h>
#include <engine/assets/compute_pipeline/compute_pipeline.h>
#include <logging/logger.h>

namespace wz::engine::assets::internal
{
    void register_compute_pipeline_compilers(
        wz::asset::CompilerRegistry& registry,
        wz::Logger& logger,
        ComputePipelineTable& table);
}
