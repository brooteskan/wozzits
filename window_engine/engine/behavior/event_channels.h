#pragma once

#include <engine/behavior/behavior_plugin_abi.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace wz::engine::behavior
{
    using EventChannelMask = uint32_t;

    enum EventChannelBit : EventChannelMask
    {
        EventChannelFrameUpdate = 1u << 0,
        EventChannelSceneLoaded = 1u << 1,
        EventChannelCollisionEnter = 1u << 2,
        EventChannelCollisionStay = 1u << 3,
        EventChannelCollisionExit = 1u << 4,
        EventChannelProximityEnter = 1u << 5,
        EventChannelProximityStay = 1u << 6,
        EventChannelProximityExit = 1u << 7,
        EventChannelInputKeyPressed = 1u << 8,
        EventChannelInputKeyReleased = 1u << 9,
        EventChannelInputMouseButtonPressed = 1u << 10,
        EventChannelInputMouseButtonReleased = 1u << 11,
        EventChannelInputControllerButtonPressed = 1u << 12,
        EventChannelInputControllerButtonReleased = 1u << 13,
        EventChannelInputControllerAxisChanged = 1u << 14,
        EventChannelGpuComputeCompleted = 1u << 15,
        EventChannelGpuComputeFailed = 1u << 16,
    };

    constexpr EventChannelMask kCollisionEventChannels =
        EventChannelCollisionEnter
        | EventChannelCollisionStay
        | EventChannelCollisionExit;

    constexpr EventChannelMask kProximityEventChannels =
        EventChannelProximityEnter
        | EventChannelProximityStay
        | EventChannelProximityExit;

    constexpr EventChannelMask kInputEventChannels =
        EventChannelInputKeyPressed
        | EventChannelInputKeyReleased
        | EventChannelInputMouseButtonPressed
        | EventChannelInputMouseButtonReleased
        | EventChannelInputControllerButtonPressed
        | EventChannelInputControllerButtonReleased
        | EventChannelInputControllerAxisChanged;

    constexpr EventChannelMask kGpuComputeEventChannels =
        EventChannelGpuComputeCompleted
        | EventChannelGpuComputeFailed;

    struct EventChannelCompileResult
    {
        EventChannelMask mask = 0u;
        uint32_t unknown_count = 0u;
        uint32_t redundant_count = 0u;
    };

    constexpr EventChannelMask channel_mask_for_token(
        std::string_view channel) noexcept
    {
        if (channel == "frame.update") {
            return EventChannelFrameUpdate;
        }
        if (channel == "scene.loaded") {
            return EventChannelSceneLoaded;
        }
        if (channel == "collision.enter") {
            return EventChannelCollisionEnter;
        }
        if (channel == "collision.stay") {
            return EventChannelCollisionStay;
        }
        if (channel == "collision.exit") {
            return EventChannelCollisionExit;
        }
        if (channel == "collision.*") {
            return kCollisionEventChannels;
        }
        if (channel == "proximity.enter") {
            return EventChannelProximityEnter;
        }
        if (channel == "proximity.stay") {
            return EventChannelProximityStay;
        }
        if (channel == "proximity.exit") {
            return EventChannelProximityExit;
        }
        if (channel == "proximity.*") {
            return kProximityEventChannels;
        }
        if (channel == "input.key.pressed") {
            return EventChannelInputKeyPressed;
        }
        if (channel == "input.key.released") {
            return EventChannelInputKeyReleased;
        }
        if (channel == "input.mouse_button.pressed") {
            return EventChannelInputMouseButtonPressed;
        }
        if (channel == "input.mouse_button.released") {
            return EventChannelInputMouseButtonReleased;
        }
        if (channel == "input.controller_button.pressed") {
            return EventChannelInputControllerButtonPressed;
        }
        if (channel == "input.controller_button.released") {
            return EventChannelInputControllerButtonReleased;
        }
        if (channel == "input.controller_axis.changed") {
            return EventChannelInputControllerAxisChanged;
        }
        if (channel == "input.*") {
            return kInputEventChannels;
        }
        if (channel == "gpu.compute.completed") {
            return EventChannelGpuComputeCompleted;
        }
        if (channel == "gpu.compute.failed") {
            return EventChannelGpuComputeFailed;
        }
        if (channel == "gpu.compute.*") {
            return kGpuComputeEventChannels;
        }
        return 0u;
    }

    constexpr EventChannelMask event_kind_to_channel_bit(
        WzBehaviorEventKind kind) noexcept
    {
        switch (kind) {
        case WZ_EVENT_FRAME_UPDATE:
            return EventChannelFrameUpdate;
        case WZ_EVENT_SCENE_LOADED:
            return EventChannelSceneLoaded;
        case WZ_EVENT_COLLISION_ENTER:
            return EventChannelCollisionEnter;
        case WZ_EVENT_COLLISION_STAY:
            return EventChannelCollisionStay;
        case WZ_EVENT_COLLISION_EXIT:
            return EventChannelCollisionExit;
        case WZ_EVENT_PROXIMITY_ENTER:
            return EventChannelProximityEnter;
        case WZ_EVENT_PROXIMITY_STAY:
            return EventChannelProximityStay;
        case WZ_EVENT_PROXIMITY_EXIT:
            return EventChannelProximityExit;
        case WZ_EVENT_INPUT_KEY_PRESSED:
            return EventChannelInputKeyPressed;
        case WZ_EVENT_INPUT_KEY_RELEASED:
            return EventChannelInputKeyReleased;
        case WZ_EVENT_INPUT_MOUSE_BUTTON_PRESSED:
            return EventChannelInputMouseButtonPressed;
        case WZ_EVENT_INPUT_MOUSE_BUTTON_RELEASED:
            return EventChannelInputMouseButtonReleased;
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_PRESSED:
            return EventChannelInputControllerButtonPressed;
        case WZ_EVENT_INPUT_CONTROLLER_BUTTON_RELEASED:
            return EventChannelInputControllerButtonReleased;
        case WZ_EVENT_INPUT_CONTROLLER_AXIS_CHANGED:
            return EventChannelInputControllerAxisChanged;
        case WZ_EVENT_GPU_COMPUTE_COMPLETED:
            return EventChannelGpuComputeCompleted;
        case WZ_EVENT_GPU_COMPUTE_FAILED:
            return EventChannelGpuComputeFailed;
        default:
            return 0u;
        }
    }

    constexpr bool channel_mask_accepts_event(
        EventChannelMask mask,
        WzBehaviorEventKind kind) noexcept
    {
        return (mask & event_kind_to_channel_bit(kind)) != 0u;
    }

    inline EventChannelCompileResult compile_channel_mask(
        std::span<const std::string> channels) noexcept
    {
        EventChannelCompileResult result{};
        for (const std::string& channel : channels) {
            const EventChannelMask token_mask =
                channel_mask_for_token(channel);
            if (token_mask == 0u) {
                ++result.unknown_count;
                continue;
            }
            if ((result.mask & token_mask) != 0u) {
                ++result.redundant_count;
            }
            result.mask |= token_mask;
        }
        return result;
    }
}
