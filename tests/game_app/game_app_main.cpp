// tests/game_app/game_app_main.cpp
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

#include <engine/game_app.h>


int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    wz::app::GameApp app{};

    if (!wz::app::init(app))
    {
        wz::app::shutdown(app);
        return 1;
    }

    wz::engine::run([&app](wz::engine::Context& ctx, wz::engine::FrameContext& fctx) {
        wz::app::update(ctx, fctx, app);
        wz::app::render(app, fctx);
    });

    wz::app::shutdown(app);
    return 0;
}
