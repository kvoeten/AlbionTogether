#pragma once

#include "../Platform/UniqueHandle.h"
#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fable::launcher::runtime
{
using fable::launcher::platform::UniqueHandle;

struct LaunchedGame final
{
    UniqueHandle process;
    UniqueHandle shutdownEvent;
    DWORD processId = 0;
    HWND window = nullptr;
};

struct GameLaunchSpec final
{
    std::filesystem::path executable;
    std::filesystem::path clientDll;
    std::filesystem::path clientLog;
    std::filesystem::path eventPath;
    std::filesystem::path fixtureDocuments;
    std::filesystem::path characterSnapshot;
    std::filesystem::path scriptData;
    std::wstring clientMode;
    std::wstring scenario;
    std::wstring runId;
    std::wstring localSession;
    std::wstring localInstance;
    std::wstring multiplayerRole;
    std::wstring multiplayerAddress;
    unsigned short multiplayerPort = 0;
    std::wstring multiplayerPlayerId;
    std::wstring multiplayerAppearance;
    bool showConsole = true;
    bool generateLogs = true;
    std::vector<std::wstring> arguments;
};

bool SpawnGame(const GameLaunchSpec &spec, LaunchedGame &launched);
bool CloseCreatedProcess(HANDLE process, DWORD processId, HANDLE shutdownEvent);
[[nodiscard]] bool IsGameProcessRunning() noexcept;
} // namespace fable::launcher::runtime
