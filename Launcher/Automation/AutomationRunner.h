#pragma once

#include "../Runtime/GameProcess.h"

#include <filesystem>
#include <string>

namespace fable::launcher::automation
{
    int RunAutomation(
        runtime::LaunchedGame& game,
        const std::filesystem::path& eventPath,
        const std::wstring& scenario,
        unsigned int timeoutSeconds,
        bool characterSnapshotExpected);
}
