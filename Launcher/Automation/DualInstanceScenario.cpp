#include "DualInstanceScenario.h"

#include "../Configuration/LauncherConstants.h"
#include "../Diagnostics/EventLog.h"
#include "WindowControl.h"

#include <TlHelp32.h>

#include <iostream>

namespace fable::launcher::automation
{
namespace
{
struct DualInstanceContext
{
    const std::filesystem::path &executable;
    const std::filesystem::path &clientDll;
    const std::filesystem::path &sessionRoot;
    const std::wstring &sessionId;
    unsigned int timeoutSeconds;
    std::vector<std::wstring> arguments;
};

std::filesystem::path RoleRoot(const DualInstanceContext &context, const wchar_t *role)
{
    return context.sessionRoot / role;
}

bool PrepareRole(const DualInstanceContext &context, const wchar_t *role)
{
    std::error_code error;
    std::filesystem::create_directories(RoleRoot(context, role) / L"Documents", error);
    if (!error)
    {
        std::filesystem::create_directories(RoleRoot(context, role) / L"script-data", error);
    }
    return !error;
}

bool SpawnRole(const DualInstanceContext &context, const wchar_t *role, runtime::LaunchedGame &game)
{
    const std::filesystem::path root = RoleRoot(context, role);
    const std::wstring runId = context.sessionId + L"-" + role;
    runtime::GameLaunchSpec spec;
    spec.executable = context.executable;
    spec.clientDll = context.clientDll;
    spec.clientLog = root / L"client.log";
    spec.eventPath = root / L"events.jsonl";
    spec.fixtureDocuments = root / L"Documents";
    spec.scriptData = root / L"script-data";
    spec.clientMode = L"observe";
    spec.scenario = L"dual_title_screen";
    spec.runId = runId;
    spec.localSession = context.sessionId;
    spec.localInstance = role;
    spec.arguments = context.arguments;
    return runtime::SpawnGame(spec, game);
}

void StopGame(runtime::LaunchedGame &game)
{
    if (game.process.valid())
    {
        runtime::CloseCreatedProcess(game.process.get(), game.processId, game.shutdownEvent.get());
    }
}

bool StartInstances(const DualInstanceContext &context, runtime::LaunchedGame &host, runtime::LaunchedGame &guest)
{
    std::wcout << L"Dual test: starting isolated host first.\n";
    if (!SpawnRole(context, L"host", host) ||
        !WaitForLocalInstanceReady(host, RoleRoot(context, L"host") / L"events.jsonl", L"host", 0,
                                   context.timeoutSeconds))
    {
        StopGame(host);
        return false;
    }

    std::wcout << L"Dual test: host is responsive; starting isolated guest.\n";
    if (!SpawnRole(context, L"guest", guest) ||
        !WaitForLocalInstanceReady(guest, RoleRoot(context, L"guest") / L"events.jsonl", L"guest",
                                   fable::launcher::kLocalTestWindowPitch, context.timeoutSeconds))
    {
        StopGame(guest);
        StopGame(host);
        return false;
    }
    if (host.processId == guest.processId || host.window == guest.window)
    {
        std::wcerr << L"Dual test failed: host and guest did not receive distinct PID and HWND identities.\n";
        StopGame(guest);
        StopGame(host);
        return false;
    }
    return true;
}

bool ObserveCoexistence(const DualInstanceContext &context, runtime::LaunchedGame &host, runtime::LaunchedGame &guest,
                        unsigned int holdSeconds)
{
    std::wcout << L"Dual test: both compact side-by-side title windows are responsive; observing " << holdSeconds
               << L" seconds of coexistence.\n";
    const ULONGLONG holdDeadline = GetTickCount64() + static_cast<ULONGLONG>(holdSeconds) * 1'000;
    while (GetTickCount64() < holdDeadline)
    {
        const bool processesAlive = WaitForSingleObject(host.process.get(), 0) == WAIT_TIMEOUT &&
                                    WaitForSingleObject(guest.process.get(), 0) == WAIT_TIMEOUT;
        const bool hooksHealthy =
            !fable::launcher::diagnostics::EventWasReported(
                fable::launcher::diagnostics::ReadEventFile(RoleRoot(context, L"host") / L"events.jsonl"),
                "ClientFailed") &&
            !fable::launcher::diagnostics::EventWasReported(
                fable::launcher::diagnostics::ReadEventFile(RoleRoot(context, L"guest") / L"events.jsonl"),
                "ClientFailed");
        if (!processesAlive || !hooksHealthy || !WindowIsResponsive(host.window) || !WindowIsResponsive(guest.window))
        {
            std::wcerr << L"Dual test failed during the coexistence interval.\n";
            StopGame(guest);
            StopGame(host);
            return false;
        }
        Sleep(500);
    }
    return true;
}
} // namespace

int RunDualInstanceTest(const std::filesystem::path &executable, const std::filesystem::path &clientDll,
                        const std::filesystem::path &sessionRoot, const std::wstring &sessionId,
                        unsigned int timeoutSeconds, unsigned int holdSeconds,
                        const std::vector<std::wstring> &originalArguments)
{
    if (AnyFableProcessIsRunning())
    {
        std::wcerr << L"Dual-instance test refused because a pre-existing Fable Anniversary process is running.\n";
        return 1;
    }
    DualInstanceContext context{executable, clientDll,      sessionRoot,
                                sessionId,  timeoutSeconds, LocalWindowArguments(originalArguments)};
    if (!PrepareRole(context, L"host") || !PrepareRole(context, L"guest"))
    {
        std::wcerr << L"Could not create isolated host and guest state roots.\n";
        return 1;
    }

    runtime::LaunchedGame host;
    runtime::LaunchedGame guest;
    if (!StartInstances(context, host, guest) || !ObserveCoexistence(context, host, guest, holdSeconds))
    {
        return 1;
    }
    const bool finalGeometry = PositionLocalWindow(host.window, L"host", 0, 0) &&
                               PositionLocalWindow(guest.window, L"guest", fable::launcher::kLocalTestWindowPitch, 0);
    const bool guestStopped =
        runtime::CloseCreatedProcess(guest.process.get(), guest.processId, guest.shutdownEvent.get());
    const bool hostStopped = runtime::CloseCreatedProcess(host.process.get(), host.processId, host.shutdownEvent.get());
    if (!finalGeometry || !guestStopped || !hostStopped)
    {
        std::wcerr << L"Dual test failed during final geometry validation or shutdown.\n";
        return 1;
    }
    std::wcout << L"Dual-instance acceptance passed: distinct responsive host and guest PIDs/HWNDs coexisted in "
                  L"isolated compact side-by-side windows.\n"
               << L"State root: " << sessionRoot.wstring() << L"\n";
    return 0;
}
} // namespace fable::launcher::automation
