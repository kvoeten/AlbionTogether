#pragma once

#include "../Runtime/GameProcess.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fable::launcher::automation
{
    int RunDualInstanceTest(
        const std::filesystem::path& executable,
        const std::filesystem::path& clientDll,
        const std::filesystem::path& sessionRoot,
        const std::wstring& sessionId,
        unsigned int timeoutSeconds,
        unsigned int holdSeconds,
        const std::vector<std::wstring>& originalArguments);
}
