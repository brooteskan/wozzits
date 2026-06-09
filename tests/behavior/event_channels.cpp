#include <engine/assets/scene/scene_instance.h>
#include <engine/behavior/event_channels.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    wz::engine::behavior::EventChannelCompileResult compile(
        std::initializer_list<const char*> channels)
    {
        std::vector<std::string> owned;
        owned.reserve(channels.size());
        for (const char* channel : channels) {
            owned.emplace_back(channel);
        }
        return wz::engine::behavior::compile_channel_mask(owned);
    }
}

TEST(EventChannels, CompilesExactKnownTokens)
{
    using namespace wz::engine::behavior;

    EXPECT_EQ(
        compile({ "collision.enter" }).mask,
        EventChannelCollisionEnter);
    EXPECT_EQ(
        compile({ "frame.update" }).mask,
        EventChannelFrameUpdate);
    EXPECT_EQ(
        compile({ "input.mouse_button.released" }).mask,
        EventChannelInputMouseButtonReleased);
    EXPECT_EQ(
        compile({ "input.controller_axis.changed" }).mask,
        EventChannelInputControllerAxisChanged);
    EXPECT_EQ(
        compile({ "gpu.compute.request" }).mask,
        EventChannelGpuComputeRequest);
    EXPECT_EQ(
        compile({ "gpu.compute.completed" }).mask,
        EventChannelGpuComputeCompleted);
    EXPECT_EQ(
        compile({ "gpu.compute.failed" }).mask,
        EventChannelGpuComputeFailed);
    EXPECT_EQ(compile({ "scene.loaded" }).mask, EventChannelSceneLoaded);
}

TEST(EventChannels, CompilesFamilyAliases)
{
    using namespace wz::engine::behavior;

    EXPECT_EQ(compile({ "collision.*" }).mask, kCollisionEventChannels);
    EXPECT_EQ(compile({ "proximity.*" }).mask, kProximityEventChannels);
    EXPECT_EQ(compile({ "input.*" }).mask, kInputEventChannels);
    EXPECT_EQ(compile({ "gpu.compute.*" }).mask, kGpuComputeEventChannels);
}

TEST(EventChannels, RedundantTokensDoNotChangeMask)
{
    using namespace wz::engine::behavior;

    const auto compiled = compile({ "collision.enter", "collision.*" });

    EXPECT_EQ(compiled.mask, kCollisionEventChannels);
    EXPECT_EQ(compiled.unknown_count, 0u);
    EXPECT_EQ(compiled.redundant_count, 1u);
}

TEST(EventChannels, UnknownAndEmptyChannelsProduceNoKnownBits)
{
    const auto unknown = compile({ "unknown.channel" });
    const auto empty = compile({});

    EXPECT_EQ(unknown.mask, 0u);
    EXPECT_EQ(unknown.unknown_count, 1u);
    EXPECT_EQ(empty.mask, 0u);
    EXPECT_EQ(empty.unknown_count, 0u);
}

TEST(EventChannels, ListenerAcceptsEventsFromCompiledMask)
{
    using namespace wz::engine::behavior;

    wz::engine::assets::EventListenerComponent listener{
        .channels = { "collision.*", "input.key.pressed" },
        .channel_mask = compile({ "collision.*", "input.key.pressed" }).mask,
    };

    EXPECT_TRUE(wz::engine::assets::listener_accepts_event(
        listener,
        WZ_EVENT_COLLISION_ENTER));
    EXPECT_TRUE(wz::engine::assets::listener_accepts_event(
        listener,
        WZ_EVENT_COLLISION_STAY));
    EXPECT_TRUE(wz::engine::assets::listener_accepts_event(
        listener,
        WZ_EVENT_INPUT_KEY_PRESSED));
    EXPECT_FALSE(wz::engine::assets::listener_accepts_event(
        listener,
        WZ_EVENT_INPUT_KEY_RELEASED));
    EXPECT_FALSE(wz::engine::assets::listener_accepts_event(
        listener,
        WZ_EVENT_FRAME_UPDATE));
}

TEST(EventChannels, EventKindMappingUsesDenseChannelBits)
{
    using namespace wz::engine::behavior;

    EXPECT_EQ(
        event_kind_to_channel_bit(WZ_EVENT_COLLISION_EXIT),
        EventChannelCollisionExit);
    EXPECT_EQ(
        event_kind_to_channel_bit(WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED),
        EventChannelInputControllerButtonPressed);
    EXPECT_EQ(
        event_kind_to_channel_bit(WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED),
        EventChannelInputControllerAxisChanged);
    EXPECT_EQ(
        event_kind_to_channel_bit(WZ_EVENT_GPU_COMPUTE_REQUEST),
        EventChannelGpuComputeRequest);
    EXPECT_EQ(
        event_kind_to_channel_bit(WZ_EVENT_GPU_COMPUTE_COMPLETED),
        EventChannelGpuComputeCompleted);
    EXPECT_EQ(
        event_kind_to_channel_bit(WZ_EVENT_GPU_COMPUTE_FAILED),
        EventChannelGpuComputeFailed);
    EXPECT_EQ(event_kind_to_channel_bit(WZ_EVENT_NONE), 0u);
}
