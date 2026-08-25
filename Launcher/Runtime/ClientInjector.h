#pragma once

#include <Windows.h>
#include <filesystem>
#include <string>

namespace fable::launcher::runtime
{
bool InjectClient(HANDLE process, const std::filesystem::path &clientDll, HMODULE &remoteClientModule,
                  std::wstring &error);
bool InitializeInjectedClient(HANDLE process, HMODULE remoteClientModule, const std::filesystem::path &clientDll,
                              std::wstring &error);
bool WaitForInjectedClientReady(HANDLE process, HMODULE remoteClientModule, const std::filesystem::path &clientDll,
                                std::wstring &error);
} // namespace fable::launcher::runtime
