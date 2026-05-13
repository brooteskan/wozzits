#include <wozzits/script/script_host.h>

#include <cstdio>
#include <cstring>

namespace
{
    bool run_checked(
        wz::script::ScriptHost* host,
        const char* name,
        const char* source)
    {
        const wz::script::RunSourceResult result =
            wz::script::run_source(host, name, source);

        if (!result.ok)
        {
            std::printf(
                "script failed: %s\n",
                result.error ? result.error : "<null>");
            return false;
        }

        if (result.value != nullptr)
        {
            std::printf("script result: %s\n", result.value);
        }

        return true;
    }

    void drain_script_logs(wz::script::ScriptHost* host)
    {
        const std::size_t count = wz::script::log_count(host);

        for (std::size_t i = 0; i < count; ++i)
        {
            std::size_t size = 0;

            const char* message =
                wz::script::log_message(host, i, &size);

            std::printf(
                "[script] %.*s\n",
                static_cast<int>(size),
                message ? message : "");
        }

        wz::script::clear_logs(host);
    }
}

int main()
{
    std::printf("Wozzits V8 script smoke\n");

    wz::script::ScriptHost* scripts =
        wz::script::create_v8_script_host();

    if (scripts == nullptr)
    {
        std::printf("failed to create script host\n");
        return 1;
    }

    if (!wz::script::initialize(scripts))
    {
        std::printf("failed to initialize script host\n");
        wz::script::destroy_v8_script_host(scripts);
        return 1;
    }

    if (!run_checked(
        scripts,
        "engine_startup.js",
        "wz.log('hello from V8 inside the engine repo');"
        "'engine startup complete';"))
    {
        wz::script::destroy_v8_script_host(scripts);
        return 1;
    }

    drain_script_logs(scripts);

    for (int frame = 0; frame < 3; ++frame)
    {
        char source[256] = {};

        std::snprintf(
            source,
            sizeof(source),
            "wz.log('engine frame %d'); %d;",
            frame,
            frame);

        if (!run_checked(scripts, "engine_frame.js", source))
        {
            wz::script::destroy_v8_script_host(scripts);
            return 1;
        }

        drain_script_logs(scripts);
    }

    wz::script::destroy_v8_script_host(scripts);

    return 0;
}