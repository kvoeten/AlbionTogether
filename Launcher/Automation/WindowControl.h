#pragma once

#include "../Runtime/GameProcess.h"

#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fable::launcher::automation
{
    HWND FindMainWindow(DWORD processId);

    bool PositionLocalWindow(
        HWND window,
        const wchar_t* instance,
        int x,
        int y);

    bool WindowIsResponsive(HWND window);

    bool WaitForLocalInstanceReady(
        runtime::LaunchedGame& game,
        const std::filesystem::path& eventPath,
        const wchar_t* instance,
        int x,
        unsigned int timeoutSeconds,
        bool requireFixtureDocuments = true);

    bool RepositionLocalInstanceWindow(
        runtime::LaunchedGame& game,
        const wchar_t* instance,
        int x,
        int y = 0,
        unsigned int timeoutMilliseconds = 5'000);

    bool AnyFableProcessIsRunning();

    std::vector<std::wstring> LocalWindowArguments(
        const std::vector<std::wstring>& original);
}
