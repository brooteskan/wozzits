#include <iostream>
#include <window/window2.h>
#include <event/platform_event.h>
#include <gpu/gpu.h>
#include <input/input.h>

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    using namespace wz::window;

    WindowDesc desc;
    desc.title = "Wozzits Window Test";
    desc.width = 800;
    desc.height = 600;

    WindowHandle window = create_window(desc);

    wz::gpu::Device device =
        wz::gpu::create_device(window);

    wz::input::init_raw_input();

    while (!window_should_close(window))
    {
        pump_messages();

        PlatformEvent event{};
        while (poll_event(window, event))
        {
            if (event.type == PlatformEvent::Type::Resize)
            {
                std::cout << "Resize: "
                    << event.resize.width << " x "
                    << event.resize.height << std::endl;

                wz::gpu::resize(
                    device,
                    event.resize.width,
                    event.resize.height
                );
            }

            if (event.type == PlatformEvent::Type::Close)
            {
                std::cout << "Close event\n";
            }
        }

        wz::gpu::begin_frame(device);
        wz::gpu::clear(device, 0.1f, 0.2f, 0.6f, 1.0f);
        wz::gpu::end_frame(device);
        wz::gpu::present(device);
    }
    wz::input::shutdown_raw_input();
    wz::gpu::destroy_device(device);
    destroy_window(window);
    

    return 0;
}