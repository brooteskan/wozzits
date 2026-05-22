#include <audio/audio_device.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    struct SineState
    {
        float phase = 0.0f;
        float frequency = 440.0f;
        float sample_rate = 48000.0f;
        float gain = 0.10f;
    };

    void sine_callback(
        wz::audio::AudioCallbackContext& context,
        void* user)
    {
        auto* state = static_cast<SineState*>(user);

        if (state == nullptr || context.output == nullptr)
        {
            return;
        }

        const float phase_step =
            2.0f * kPi * state->frequency / state->sample_rate;

        for (uint32_t frame = 0; frame < context.frames; ++frame)
        {
            const float sample = state->gain * std::sin(state->phase);

            state->phase += phase_step;

            if (state->phase >= 2.0f * kPi)
            {
                state->phase -= 2.0f * kPi;
            }

            for (uint32_t channel = 0; channel < context.channels; ++channel)
            {
                context.output[frame * context.channels + channel] = sample;
            }
        }
    }
}

int main()
{
    std::printf("Wozzits RtAudio smoke\n");

    SineState sine{};
    sine.sample_rate = 48000.0f;
    sine.frequency = 440.0f;
    sine.gain = 0.10f;

    wz::audio::DeviceDesc desc{};
    desc.format.sample_rate = 48000;
    desc.format.channels = 2;
    desc.format.frames_per_buffer = 256;
    desc.callback = &sine_callback;
    desc.user = &sine;

    wz::audio::Device* audio = wz::audio::create_device(desc);

    if (audio == nullptr)
    {
        std::printf("failed to create audio device\n");
        return 1;
    }

    std::printf(
        "created audio device: %u Hz, %u channels, %u frames/buffer\n",
        desc.format.sample_rate,
        desc.format.channels,
        desc.format.frames_per_buffer);

    if (!wz::audio::start(audio))
    {
        std::printf("failed to start audio device\n");
        wz::audio::destroy_device(audio);
        return 1;
    }

    std::printf("playing 440 Hz sine for 2 seconds...\n");

    std::this_thread::sleep_for(std::chrono::seconds(2));

    wz::audio::stop(audio);
    wz::audio::destroy_device(audio);

    std::printf("audio smoke complete\n");

    return 0;
}