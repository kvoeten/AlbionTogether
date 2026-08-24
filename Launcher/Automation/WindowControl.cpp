#include "WindowControl.h"

#include "../Configuration/LauncherConstants.h"
#include "../Diagnostics/EventLog.h"
#include "../Platform/UniqueHandle.h"

#include <TlHelp32.h>

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <iostream>
#include <iterator>

namespace fable::launcher::automation
{
    namespace
    {
        struct ProcessWindowSearch
        {
            DWORD processId = 0;
            HWND bestWindow = nullptr;
            unsigned long long bestArea = 0;
            bool bestVisible = false;
        };

        BOOL CALLBACK FindProcessWindow(HWND window, LPARAM parameter)
        {
            auto& search = *reinterpret_cast<ProcessWindowSearch*>(parameter);
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (processId != search.processId)
            {
                return TRUE;
            }

            wchar_t className[64] = {};
            GetClassNameW(window, className, static_cast<int>(std::size(className)));
            if (std::wcscmp(className, L"#32770") == 0)
            {
                return TRUE;
            }

            RECT client = {};
            if (!GetClientRect(window, &client))
            {
                return TRUE;
            }
            const LONG width = client.right - client.left;
            const LONG height = client.bottom - client.top;
            const auto area = width > 0 && height > 0
                ? static_cast<unsigned long long>(width) *
                    static_cast<unsigned long long>(height)
                : 0;
            const bool visible = IsWindowVisible(window) != FALSE;
            if ((visible && !search.bestVisible) ||
                (visible == search.bestVisible && area > search.bestArea))
            {
                search.bestArea = area;
                search.bestWindow = window;
                search.bestVisible = visible;
            }
            return TRUE;
        }
    }

    HWND FindMainWindow(DWORD processId)
    {
        ProcessWindowSearch search;
        search.processId = processId;
        EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
        return search.bestWindow;
    }

    bool PositionLocalWindow(
        HWND window,
        const wchar_t* instance,
        int x,
        int y)
    {
        if (window == nullptr)
        {
            return false;
        }

        std::wstring title = L"Fable Anniversary - FableTogether local ";
        title.append(instance != nullptr ? instance : L"instance");
        SetWindowTextW(window, title.c_str());

        // The launcher itself is DPI-unaware, so SetWindowPos would otherwise
        // virtualize 830x620 to 1245x930 at the development desktop's 150%
        // scale. Temporarily use a DPI-aware caller context: these constants
        // then mean actual screen pixels and two clients really fit side by
        // side.
        const DPI_AWARENESS_CONTEXT previousDpiContext =
            SetThreadDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const bool positioned = SetWindowPos(
            window,
            HWND_TOP,
            x,
            y,
            fable::launcher::kLocalTestWindowWidth,
            fable::launcher::kLocalTestWindowHeight,
            SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE) != FALSE;
        if (previousDpiContext != nullptr)
        {
            SetThreadDpiAwarenessContext(previousDpiContext);
        }
        return positioned;
    }

    bool WindowIsResponsive(HWND window)
    {
        if (window == nullptr || !IsWindow(window) || IsHungAppWindow(window))
        {
            return false;
        }
        DWORD_PTR ignored = 0;
        return SendMessageTimeoutW(
            window,
            WM_NULL,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            1'000,
            &ignored) != 0;
    }

    bool WaitForLocalInstanceReady(
        runtime::LaunchedGame& game,
        const std::filesystem::path& eventPath,
        const wchar_t* instance,
        int x,
        unsigned int timeoutSeconds)
    {
        const ULONGLONG deadline = GetTickCount64() +
            static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
        for (;;)
        {
            const std::string events =
                fable::launcher::diagnostics::ReadEventFile(eventPath);
            if (fable::launcher::diagnostics::EventWasReported(
                    events, "ClientFailed"))
            {
                std::wcerr << L"Local instance " << instance
                           << L" reported a client hook failure.\n";
                return false;
            }
            if (fable::launcher::diagnostics::EventWasReported(
                    events, "ClientHooksReady") &&
                fable::launcher::diagnostics::EventWasReported(
                    events, "FrontEndStartReady") &&
                fable::launcher::diagnostics::EventWasReported(
                    events, "LocalInstanceReady") &&
                fable::launcher::diagnostics::EventWasReported(
                    events, "UnrealSingletonNamespaced") &&
                fable::launcher::diagnostics::EventWasReported(
                    events, "FixtureDocumentsRedirectReady") &&
                fable::launcher::diagnostics::EventWasReported(
                    events, "ScriptStorageRootReady"))
            {
                game.window = FindMainWindow(game.processId);
                if (game.window != nullptr &&
                    PositionLocalWindow(game.window, instance, x, 0) &&
                    WindowIsResponsive(game.window))
                {
                    return true;
                }
            }

            if (WaitForSingleObject(game.process.get(), 250) == WAIT_OBJECT_0)
            {
                DWORD exitCode = 0;
                GetExitCodeProcess(game.process.get(), &exitCode);
                std::wcerr << L"Local instance " << instance
                           << L" exited before its title window was ready; exit code "
                           << exitCode << L".\n";
                return false;
            }
            if (GetTickCount64() >= deadline)
            {
                std::wcerr << L"Local instance " << instance
                           << L" timed out before its title window was ready.\n";
                return false;
            }
        }
    }

    bool RepositionLocalInstanceWindow(
        runtime::LaunchedGame& game,
        const wchar_t* instance,
        int x,
        int y,
        unsigned int timeoutMilliseconds)
    {
        const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;
        do
        {
            const HWND currentWindow = FindMainWindow(game.processId);
            if (currentWindow != nullptr)
            {
                game.window = currentWindow;
            }
            if (game.window != nullptr && IsWindow(game.window) &&
                PositionLocalWindow(game.window, instance, x, y))
            {
                return true;
            }
            if (!game.process.valid() ||
                WaitForSingleObject(game.process.get(), 100) == WAIT_OBJECT_0)
            {
                return false;
            }
        } while (GetTickCount64() < deadline);
        return false;
    }

    bool AnyFableProcessIsRunning()
    {
        fable::launcher::platform::UniqueHandle snapshot(
            CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snapshot.valid())
        {
            return true;
        }
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot.get(), &entry))
        {
            return true;
        }
        do
        {
            if (_wcsicmp(
                    entry.szExeFile,
                    fable::launcher::kGameExecutableName) == 0)
            {
                return true;
            }
        } while (Process32NextW(snapshot.get(), &entry));
        return false;
    }

    std::vector<std::wstring> LocalWindowArguments(
        const std::vector<std::wstring>& original)
    {
        std::vector<std::wstring> arguments = original;
        const auto addIfMissing = [&](const wchar_t* value)
        {
            const bool present = std::any_of(
                arguments.begin(),
                arguments.end(),
                [value](const std::wstring& argument)
                {
                    return _wcsicmp(argument.c_str(), value) == 0;
                });
            if (!present)
            {
                arguments.emplace_back(value);
            }
        };
        addIfMissing(L"-windowed");
        addIfMissing(L"-ResX=1280");
        addIfMissing(L"-ResY=720");
        addIfMissing(L"-nomoviestartup");
        return arguments;
    }
}
