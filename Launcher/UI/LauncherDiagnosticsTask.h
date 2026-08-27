#pragma once

#include "../Diagnostics/LauncherDiagnostics.h"

#include <Windows.h>

#include <filesystem>
#include <string>
#include <thread>

namespace fable::launcher::ui
{
    class LauncherDiagnosticsTask final
    {
    public:
        LauncherDiagnosticsTask() = default;
        ~LauncherDiagnosticsTask();

        LauncherDiagnosticsTask(const LauncherDiagnosticsTask&) = delete;
        LauncherDiagnosticsTask& operator=(const LauncherDiagnosticsTask&) = delete;

        [[nodiscard]] bool Start(
            HWND destination,
            UINT completionMessage,
            const std::filesystem::path& executable,
            const std::wstring& host,
            unsigned short port);
        void Join();
        [[nodiscard]] bool IsRunning() const noexcept;

    private:
        std::thread worker_;
        bool running_ = false;
    };
}
