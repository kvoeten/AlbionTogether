#pragma once

#include "../Configuration/LauncherSettings.h"

#include <string>

namespace fable::launcher::application
{
    enum class InteractiveRole
    {
        Host,
        Guest,
    };

    struct InteractiveLaunchRequest final
    {
        InteractiveRole role = InteractiveRole::Host;
        LauncherSettings settings;
        std::filesystem::path gameExecutable;
    };

    [[nodiscard]] bool LaunchInteractiveGame(
        const InteractiveLaunchRequest& request,
        std::wstring& result);
}
