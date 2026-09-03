#include "GameProcess.h"
#include "../Configuration/LauncherConstants.h"
#include "../Platform/Win32Error.h"
#include "ClientInjector.h"
#include <TlHelp32.h>
#include <algorithm>
#include <cwchar>
#include <iostream>
#include <utility>

namespace fable::launcher::runtime
{
namespace fs = std::filesystem;
using fable::launcher::kCharacterSnapshotEnvironment;
using fable::launcher::kClientModeEnvironment;
using fable::launcher::kConsoleEnabledEnvironment;
using fable::launcher::kClientPreResumeReady;
using fable::launcher::kClientRuntimeReady;
using fable::launcher::kFableSteamAppId;
using fable::launcher::kFixtureDocumentsEnvironment;
using fable::launcher::kFixtureSaveNameEnvironment;
using fable::launcher::kInjectionTimeoutMilliseconds;
using fable::launcher::kLocalInstanceEnvironment;
using fable::launcher::kLocalSessionEnvironment;
using fable::launcher::kLogPathEnvironment;
using fable::launcher::kLogFilesEnabledEnvironment;
using fable::launcher::kMultiplayerAddressEnvironment;
using fable::launcher::kMultiplayerAppearanceEnvironment;
using fable::launcher::kMapStressSeedEnvironment;
using fable::launcher::kMapStressTransitionsEnvironment;
using fable::launcher::kMultiplayerPlayerIdEnvironment;
using fable::launcher::kMultiplayerPortEnvironment;
using fable::launcher::kMultiplayerRoleEnvironment;
using fable::launcher::kRunIdEnvironment;
using fable::launcher::kScenarioEnvironment;
using fable::launcher::kScriptDataEnvironment;
using fable::launcher::kShutdownEventPrefix;
using fable::launcher::platform::FormatWindowsError;
using fable::launcher::platform::UniqueHandle;

bool IsGameProcessRunning() noexcept
{
    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid())
    {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry))
    {
        return false;
    }
    do
    {
        if (_wcsicmp(entry.szExeFile, kGameExecutableName) == 0)
        {
            return true;
        }
    } while (Process32NextW(snapshot.get(), &entry));
    return false;
}

static std::wstring QuoteArgument(const std::wstring &argument)
{
    if (argument.find_first_of(L" \t\"") == std::wstring::npos)
        return argument;
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument)
    {
        if (character == L'\\')
            ++backslashes;
        else if (character == L'\"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashes = 0;
        }
        else
        {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

static std::wstring BuildCommandLine(const fs::path &executable, const std::vector<std::wstring> &arguments)
{
    std::wstring commandLine = QuoteArgument(executable.wstring());
    for (const std::wstring &argument : arguments)
    {
        commandLine.push_back(L' ');
        commandLine.append(QuoteArgument(argument));
    }
    return commandLine;
}

static HWND FindMainWindowForProcess(DWORD processId)
{
    struct Search
    {
        DWORD pid;
        HWND best = nullptr;
        unsigned long long area = 0;
        bool visible = false;
    } search{processId};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL
        {
            auto &search = *reinterpret_cast<Search *>(parameter);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid != search.pid)
                return TRUE;
            wchar_t className[64] = {};
            GetClassNameW(window, className, 64);
            if (std::wcscmp(className, L"#32770") == 0)
                return TRUE;
            RECT client = {};
            if (!GetClientRect(window, &client))
                return TRUE;
            const LONG width = client.right - client.left, height = client.bottom - client.top;
            const auto area = width > 0 && height > 0
                                  ? static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height)
                                  : 0;
            const bool visible = IsWindowVisible(window) != FALSE;
            if ((visible && !search.visible) || (visible == search.visible && area > search.area))
            {
                search.area = area;
                search.best = window;
                search.visible = visible;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.best;
}

bool TerminateAndConfirmExit(HANDLE process, DWORD exitCode)
{
    if (process == nullptr)
    {
        std::wcerr << L"Automation: cannot terminate an invalid process handle.\n";
        return false;
    }
    if (!TerminateProcess(process, exitCode))
    {
        const DWORD error = GetLastError();
        std::wcerr << L"Automation: TerminateProcess failed (" << error << L"): "
                   << FormatWindowsError(error) << L".\n";
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(process, 5'000);
    if (waitResult == WAIT_OBJECT_0)
    {
        return true;
    }
    if (waitResult == WAIT_FAILED)
    {
        const DWORD error = GetLastError();
        std::wcerr << L"Automation: waiting for terminated process failed (" << error << L"): "
                   << FormatWindowsError(error) << L".\n";
    }
    else
    {
        std::wcerr << L"Automation: terminated process did not exit within five seconds.\n";
    }
    return false;
}

bool CloseCreatedProcess(HANDLE process, DWORD processId, HANDLE shutdownEvent)
{
    if (shutdownEvent != nullptr)
    {
        std::wcout << L"Automation: requesting shutdown through the run-scoped "
                      L"client event.\n";
        if (!SetEvent(shutdownEvent))
            std::wcerr << L"Automation: could not signal the run-scoped "
                          L"shutdown event.\n";
    }
    else
    {
        HWND window = nullptr;
        const ULONGLONG deadline = GetTickCount64() + 2'000;
        do
        {
            window = FindMainWindowForProcess(processId);
            if (window != nullptr)
                break;
            if (WaitForSingleObject(process, 100) == WAIT_OBJECT_0)
                return true;
        } while (GetTickCount64() < deadline);
        if (window != nullptr)
        {
            std::wcout << L"Automation: requesting graceful shutdown through the "
                          L"game window.\n";
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
    const DWORD gracefulWait = WaitForSingleObject(process, 15'000);
    if (gracefulWait == WAIT_OBJECT_0)
        return true;
    if (gracefulWait == WAIT_FAILED)
    {
        const DWORD error = GetLastError();
        std::wcerr << L"Automation: waiting for graceful shutdown failed (" << error << L"): "
                   << FormatWindowsError(error) << L".\n";
    }
    std::wcerr << L"Automation: graceful shutdown timed out; terminating only PID " << processId << L".\n";
    TerminateAndConfirmExit(process, ERROR_TIMEOUT);
    return false;
}

bool BootstrapInjectedClient(HANDLE process, HANDLE primaryThread, const fs::path &clientDll, DWORD processId,
                             LaunchedGame &launched)
{
    // Initialization runs against the suspended game; readiness is queried
    // only after the primary thread resumes.
    std::wstring injectionError;
    HMODULE remoteClientModule = nullptr;
    std::wcout << L"Inject: loading " << clientDll.wstring() << L".\n";
    std::wcout.flush();
    if (!InjectClient(process, clientDll, remoteClientModule, injectionError))
    {
        TerminateAndConfirmExit(process, ERROR_DLL_INIT_FAILED);
        std::wcerr << L"Injection failed; the suspended game process was terminated: " << injectionError << L'\n';
        return false;
    }
    std::wcout << L"Inject: client DLL loaded; initializing before resume.\n";
    std::wcout.flush();
    if (!InitializeInjectedClient(process, remoteClientModule, clientDll, injectionError))
    {
        TerminateAndConfirmExit(process, ERROR_DLL_INIT_FAILED);
        std::wcerr << L"Client initialization failed; the suspended game process "
                      L"was terminated: "
                   << injectionError << L'\n';
        return false;
    }
    std::wcout << L"Inject: pre-resume initialization validated; resuming the "
                  L"primary thread.\n";
    if (ResumeThread(primaryThread) == static_cast<DWORD>(-1))
    {
        const DWORD code = GetLastError();
        TerminateAndConfirmExit(process, code);
        std::wcerr << L"Could not resume the game (" << code << L"): " << FormatWindowsError(code) << L'\n';
        return false;
    }
    std::wcout << L"Inject: waiting for the client runtime to become ready.\n";
    std::wcout.flush();
    if (!WaitForInjectedClientReady(process, remoteClientModule, clientDll, injectionError))
    {
        TerminateAndConfirmExit(process, ERROR_DLL_INIT_FAILED);
        std::wcerr << L"Client runtime startup failed; the game process was "
                      L"terminated: "
                   << injectionError << L'\n';
        return false;
    }
    std::wcout << L"Fable Anniversary started with AlbionTogether.Client.dll; "
                  L"runtime ready (PID "
               << processId << L").\n";
    launched.processId = processId;
    return true;
}

class ChildEnvironment final
{
  public:
    explicit ChildEnvironment(const GameLaunchSpec &spec)
    {
        LPWCH inherited = GetEnvironmentStringsW();
        if (inherited == nullptr)
        {
            return;
        }
        for (LPWCH entry = inherited; *entry != L'\0'; entry += wcslen(entry) + 1)
        {
            entries_.emplace_back(entry);
        }
        FreeEnvironmentStringsW(inherited);

        Override(L"SteamAppId", kFableSteamAppId);
        Override(L"SteamGameId", kFableSteamAppId);
        Override(kClientModeEnvironment, spec.clientMode);
        Override(kScenarioEnvironment, spec.scenario);
        Override(kRunIdEnvironment, spec.runId);
        Override(kEventPathEnvironment,
            spec.generateLogs ? spec.eventPath.wstring() : L"");
        Override(kLogPathEnvironment,
            spec.generateLogs ? spec.clientLog.wstring() : L"");
        Override(kConsoleEnabledEnvironment, spec.showConsole ? L"1" : L"0");
        Override(kLogFilesEnabledEnvironment, spec.generateLogs ? L"1" : L"0");
        Override(kFixtureDocumentsEnvironment, spec.fixtureDocuments.wstring());
        Override(kFixtureSaveNameEnvironment, spec.fixtureSaveName);
        Override(kCharacterSnapshotEnvironment, spec.characterSnapshot.wstring());
        Override(kScriptDataEnvironment, spec.scriptData.wstring());
        Override(kLocalSessionEnvironment, spec.localSession);
        Override(kLocalInstanceEnvironment, spec.localInstance);
        Override(kMultiplayerRoleEnvironment, spec.multiplayerRole);
        Override(kMultiplayerAddressEnvironment, spec.multiplayerAddress);
        Override(kMultiplayerPortEnvironment,
                 spec.multiplayerRole.empty() ? L"" : std::to_wstring(spec.multiplayerPort));
        Override(kMultiplayerPlayerIdEnvironment, spec.multiplayerPlayerId);
        Override(kMultiplayerAppearanceEnvironment, spec.multiplayerAppearance);
        Override(kMapStressSeedEnvironment,
            spec.mapStressSeed == 0 ? L"" :
                std::to_wstring(spec.mapStressSeed));
        Override(kMapStressTransitionsEnvironment,
            spec.mapStressTransitions == 0 ? L"" :
                std::to_wstring(spec.mapStressTransitions));
        BuildBlock();
    }

    [[nodiscard]] bool Applied() const
    {
        return !block_.empty();
    }

    [[nodiscard]] wchar_t *data()
    {
        return block_.data();
    }

  private:
    static bool SameName(const std::wstring &entry, const std::wstring &name)
    {
        const std::size_t delimiter = entry.find(L'=');
        const std::size_t nameLength = delimiter == std::wstring::npos ? entry.size() : delimiter;
        return nameLength == name.size() && _wcsnicmp(entry.c_str(), name.c_str(), nameLength) == 0;
    }

    void Override(const wchar_t *name, const std::wstring &value)
    {
        Override(std::wstring(name), value);
    }

    void Override(const std::wstring &name, const std::wstring &value)
    {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&name](const std::wstring &entry) { return SameName(entry, name); }),
                       entries_.end());
        entries_.push_back(name + L"=" + value);
    }

    void BuildBlock()
    {
        std::sort(entries_.begin(), entries_.end(), [](const std::wstring &left, const std::wstring &right) {
            return _wcsicmp(left.c_str(), right.c_str()) < 0;
        });
        for (const std::wstring &entry : entries_)
        {
            block_.insert(block_.end(), entry.begin(), entry.end());
            block_.push_back(L'\0');
        }
        block_.push_back(L'\0');
    }

    std::vector<std::wstring> entries_;
    std::vector<wchar_t> block_;
};

bool SpawnGame(const GameLaunchSpec &spec, LaunchedGame &launched)
{
    const fs::path &executable = spec.executable;
    const fs::path &clientDll = spec.clientDll;
    const fs::path &clientLog = spec.clientLog;
    const std::wstring &scenario = spec.scenario;
    const std::wstring &runId = spec.runId;
    const std::wstring &localInstance = spec.localInstance;
    const std::vector<std::wstring> &arguments = spec.arguments;
    launched = {};
    if (spec.generateLogs)
    {
        std::error_code logError;
        fs::remove(clientLog, logError);
        if (logError)
            std::wcerr << L"Log:    could not clear the previous log: " << logError.message().c_str() << L'\n';
    }
    std::wstring commandLine = BuildCommandLine(executable, arguments);
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    const std::wstring workingDirectory = executable.parent_path().wstring();
    ChildEnvironment environment(spec);
    // Build a private inherited environment block so sequential peer launches
    // never mutate or race the launcher's own environment variables.
    if (!environment.Applied())
    {
        std::wcerr << L"Could not prepare the child-process environment for Fable "
                      L"Anniversary.\n";
        return false;
    }
    UniqueHandle shutdownEvent;
    if (!scenario.empty())
    {
        shutdownEvent =
            UniqueHandle(CreateEventW(nullptr, TRUE, FALSE, (std::wstring(kShutdownEventPrefix) + runId).c_str()));
        if (!shutdownEvent.valid())
        {
            std::wcerr << L"Could not create the run-scoped automation "
                          L"shutdown event.\n";
            return false;
        }
    }
    std::wcout << L"Steam:  App ID " << kFableSteamAppId << L" supplied to the child process.\n";
    if (!localInstance.empty())
    {
        std::wcout << L"Identity: local development peer " << localInstance
                   << L"; Steam identity is not used for peer identity.\n";
    }
    std::wcout << L"Launch: creating Fable Anniversary suspended; working directory " << workingDirectory << L".\n";
    if (!CreateProcessW(executable.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment.data(), workingDirectory.c_str(),
                        &startupInfo, &processInfo))
    {
        const DWORD code = GetLastError();
        std::wcerr << L"Failed to start Fable Anniversary (" << code << L"): " << FormatWindowsError(code) << L'\n';
        return false;
    }
    UniqueHandle process(processInfo.hProcess), primaryThread(processInfo.hThread);
    std::wcout << L"Launch: suspended process created (PID " << processInfo.dwProcessId << L").\n";
    if (!BootstrapInjectedClient(process.get(), primaryThread.get(), clientDll, processInfo.dwProcessId, launched))
        return false;
    launched.process = std::move(process);
    launched.shutdownEvent = std::move(shutdownEvent);
    return true;
}

} // namespace fable::launcher::runtime
