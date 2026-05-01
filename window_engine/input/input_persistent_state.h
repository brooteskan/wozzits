#pragma once

namespace wz::input::internal
{
    struct InputPersistentState
    {
        bool keys_down[256]{};
        bool mouse_buttons_down[3]{};

        int mouse_x = 0;
        int mouse_y = 0;
    };
}