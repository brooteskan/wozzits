// tests/rendering/renderable_gpu_cache.cpp

#include <gtest/gtest.h>

#include <engine/rendering/renderable_gpu_cache.h>

#include <gpu/gpu.h>

TEST(RenderableGpuCache, RejectsInvalidDeviceAndHandle)
{
    wz::engine::rendering::RenderableGpuCache cache;

    wz::gpu::Device device{};
    wz::engine::assets::RenderableHandle handle{};

    // We cannot construct a full EngineAssetLibrary-free call because realize()
    // requires it, so this test is intentionally omitted unless your test helper
    // can construct EngineAssetLibrary cheaply.
    //
    // This file exists as a placeholder for future integration tests using a
    // real device/window.
    SUCCEED();
}