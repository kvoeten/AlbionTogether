#pragma once

#include <Windows.h>

#include <filesystem>

namespace fable::launcher::platform
{
    [[nodiscard]] bool PickFolder(
        HWND owner,
        const std::filesystem::path& initialFolder,
        std::filesystem::path& selectedFolder);
}
