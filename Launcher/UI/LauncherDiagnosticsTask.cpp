#include "LauncherDiagnosticsTask.h"

#include <memory>

namespace fable::launcher::ui
{
    LauncherDiagnosticsTask::~LauncherDiagnosticsTask()
    {
        Join();
    }

    bool LauncherDiagnosticsTask::Start(
        const HWND destination,
        const UINT completionMessage,
        const std::filesystem::path& executable,
        const std::wstring& host,
        const unsigned short port)
    {
        if (running_)
        {
            return false;
        }
        Join();
        running_ = true;
        worker_ = std::thread(
            [destination, completionMessage, executable, host, port]()
            {
                auto report = std::make_unique<
                    diagnostics::LauncherDiagnosticsReport>(
                        diagnostics::RunLauncherDiagnostics(
                            executable, host, port));
                if (IsWindow(destination) && PostMessageW(
                        destination,
                        completionMessage,
                        0,
                        reinterpret_cast<LPARAM>(report.get())))
                {
                    report.release();
                }
            });
        return true;
    }

    void LauncherDiagnosticsTask::Join()
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
        running_ = false;
    }

    bool LauncherDiagnosticsTask::IsRunning() const noexcept
    {
        return running_;
    }
}
