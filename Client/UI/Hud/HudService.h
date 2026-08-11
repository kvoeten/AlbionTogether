#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"

#include <string>

namespace fable::game::native
{
    class GameInterfaceAccess;
}

namespace fable::ui
{
    class HudService final
    {
    public:
        bool Initialize(
            game::native::GameInterfaceAccess& gameInterface,
            const core::Diagnostics& diagnostics);
        bool ShowMessage(const std::string& textGroup, int selectionMethod = 2);

    private:
        game::native::GameInterfaceAccess* gameInterface_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
