// tests/gpu/timeline_exclusivity.cpp
//
// B2-T4 (#311): the frame-timeline exclusivity contract. frame_timeline_value
// names the value end_frame will signal for the frame being recorded; the rhi
// registry touches resources with it and reclaims once completed_timeline_value
// passes it. That is only sound if NOTHING ELSE signals the value mid-frame —
// a one-shot upload that signals the frame fence would complete the value
// while the frame's draws are still unsubmitted, so any release + collect in
// that window destroys resources the recorded frame still references. Pins
// that one-shot uploads (buffer + texture families) ride their own copy fence.

#include <gtest/gtest.h>

#include <gpu/gpu.h>
#include <gpu/compute.h>
#include <gpu/texture.h>
#include <gpu/dx12/dx12_internal.h>

#include <window/window2.h>

#include <cstdint>

namespace
{
    namespace gi = wz::gpu::dx12::internal;
}

TEST(GpuTimelineExclusivity, OneShotUploadsDoNotSignalTheFrameTimeline)
{
    wz::window::WindowDesc window_desc{};
    window_desc.title = "gpu_timeline_exclusivity_test";
    window_desc.width = 256;
    window_desc.height = 256;
    window_desc.resizable = false;

    wz::window::WindowHandle window = wz::window::create_window(window_desc);
    if (!window.valid()) {
        GTEST_SKIP() << "no window available for the timeline test";
    }
    wz::gpu::Device device = wz::gpu::create_device(window);
    if (!device.valid()) {
        wz::window::destroy_window(window);
        GTEST_SKIP() << "no GPU device available for the timeline test";
    }

    const std::uint32_t initial[4] = { 1u, 2u, 3u, 4u };
    wz::gpu::ComputeBufferDesc buffer_desc{};
    buffer_desc.element_count = 4;
    buffer_desc.stride_bytes = sizeof(std::uint32_t);
    buffer_desc.initial_data = initial;
    buffer_desc.initial_data_bytes = sizeof(initial);
    const wz::gpu::GPUHandle buffer =
        wz::gpu::create_structured_buffer(device, buffer_desc);
    ASSERT_TRUE(buffer.valid());

    wz::gpu::TextureDesc tex_desc{};
    tex_desc.width = 4;
    tex_desc.height = 4;
    tex_desc.format = wz::gpu::TextureFormat::RGBA8Unorm;
    const wz::gpu::GPUHandle texture = wz::gpu::create_texture(device, tex_desc);
    ASSERT_TRUE(texture.valid());

    ASSERT_TRUE(wz::gpu::begin_frame(device));
    const std::uint64_t frame_value = wz::gpu::frame_timeline_value(device);
    ASSERT_GT(frame_value, 0u);

    // Mid-frame one-shot uploads: a buffer update and a texture update, the
    // two upload families the engine performs while a frame is recording.
    const std::uint32_t updated[4] = { 5u, 6u, 7u, 8u };
    ASSERT_TRUE(gi::update_compute_buffer_dx12(
        device, buffer, updated, sizeof(updated), 0));

    std::uint8_t texels[4u * 4u * 4u] = {};
    ASSERT_TRUE(wz::gpu::update_texture(
        device, texture, texels, sizeof(texels), 0));

    // The frame's signal value is untouched — no one-shot consumed it — and it
    // is NOT completed while the frame is still recording.
    EXPECT_EQ(wz::gpu::frame_timeline_value(device), frame_value)
        << "a one-shot upload consumed the frame-timeline value mid-frame";
    EXPECT_LT(wz::gpu::completed_timeline_value(device), frame_value)
        << "the frame-timeline value completed before the frame was submitted "
           "— a mid-frame collect would destroy resources this frame's "
           "recorded draws still reference";

    // The frame-list update path is legal HERE, between begin and end_frame --
    // it records into the frame's list, so the copy lands in queue order with
    // the draws around it.
    EXPECT_TRUE(gi::record_compute_buffer_update_dx12(
        device, buffer, updated, sizeof(updated), 0));

    ASSERT_TRUE(wz::gpu::end_frame(device));

    // ...and refused OUTSIDE one. `cmd` is the persistent frame list, so it is
    // still non-null here and the old code recorded into it happily -- into a
    // CLOSED list, which is a D3D12 error, with the staging parked in a frame
    // arena the next begin_frame drains without ever submitting the copy.
    // Enforced now rather than left to the header's say-so.
    EXPECT_FALSE(gi::record_compute_buffer_update_dx12(
        device, buffer, updated, sizeof(updated), 0))
        << "record_compute_buffer_update_dx12 must refuse outside begin/end_frame";

    // The immediate one-shot stays legal on both sides of the frame boundary --
    // it builds and executes its own list. Pinned so the enforcement above is
    // not later widened into "no uploads outside a frame" (or, as was tried,
    // "no one-shots inside one" -- that breaks the mid-frame uploads above).
    EXPECT_TRUE(gi::update_compute_buffer_dx12(
        device, buffer, updated, sizeof(updated), 0));

    // #306: end_frame submits and SIGNALS the frame value but no longer waits on the
    // GPU -- the pacing wait moved to the next begin_frame's slot reuse -- so the
    // value is not complete the instant end_frame returns. Flush explicitly to
    // observe it complete: wait_idle signals and waits a LATER value, and the frame
    // value is signalled exactly once, so once wait_idle returns the frame value has
    // necessarily passed.
    wz::gpu::wait_idle(device);
    EXPECT_GE(wz::gpu::completed_timeline_value(device), frame_value);

    wz::gpu::release_compute_buffer(device, buffer);
    wz::gpu::release_texture(device, texture);
    wz::gpu::destroy_device(device);
    wz::window::destroy_window(window);
}
