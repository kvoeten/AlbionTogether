#include "ClientInjector.h"

#include "../Configuration/LauncherConstants.h"
#include "../Platform/UniqueHandle.h"
#include "../Platform/Win32Error.h"

#include <cstdint>
#include <iostream>

namespace fable::launcher::runtime
{
static_assert(sizeof(void *) == 4, "Launcher injection requires the Win32 target architecture.");

namespace
{
using fable::launcher::kClientPreResumeReady;
using fable::launcher::kClientRuntimeReady;
using fable::launcher::kInjectionTimeoutMilliseconds;
using fable::launcher::kRuntimeReadyTimeoutMilliseconds;
using fable::launcher::platform::FormatWindowsError;
using fable::launcher::platform::UniqueHandle;

std::wstring WindowsFailure(const wchar_t *operation, DWORD code)
{
    return std::wstring(operation) + L" (" + std::to_wstring(code) + L"): " + FormatWindowsError(code);
}

void *AllocateRemotePath(HANDLE process, const std::wstring &dllPath, SIZE_T &bytes, std::wstring &error)
{
    bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void *remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remotePath != nullptr)
        return remotePath;
    error = WindowsFailure(L"VirtualAllocEx failed", GetLastError());
    return nullptr;
}

bool WriteRemotePath(HANDLE process, void *remotePath, const std::wstring &dllPath, SIZE_T bytes, std::wstring &error)
{
    SIZE_T bytesWritten = 0;
    if (WriteProcessMemory(process, remotePath, dllPath.c_str(), bytes, &bytesWritten) && bytesWritten == bytes)
        return true;
    error = WindowsFailure(L"WriteProcessMemory failed", GetLastError());
    return false;
}

LPTHREAD_START_ROUTINE ResolveLoadLibrary(std::wstring &error)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr)
    {
        error = WindowsFailure(L"Could not resolve kernel32.dll", GetLastError());
        return nullptr;
    }
    FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
    if (loadLibrary == nullptr)
    {
        error = WindowsFailure(L"Could not resolve LoadLibraryW", GetLastError());
        return nullptr;
    }
    return reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary);
}

bool WaitForThread(HANDLE thread, DWORD timeout, const wchar_t *timeoutMessage, const wchar_t *waitMessage,
                   std::wstring &error)
{
    const DWORD waitResult = WaitForSingleObject(thread, timeout);
    if (waitResult == WAIT_OBJECT_0)
        return true;
    if (waitResult == WAIT_TIMEOUT)
    {
        error = timeoutMessage;
        return false;
    }
    if (waitResult == WAIT_FAILED)
    {
        error = WindowsFailure(waitMessage, GetLastError());
        return false;
    }
    error = waitMessage;
    return false;
}

void ReleaseRemotePath(HANDLE process, void *remotePath)
{
    if (VirtualFreeEx(process, remotePath, 0, MEM_RELEASE) == FALSE)
    {
        const DWORD error = GetLastError();
        std::wcerr << L"VirtualFreeEx failed while releasing the remote DLL path (" << error << L"): "
                   << FormatWindowsError(error) << L".\n";
    }
}

bool LoadClientIntoProcess(HANDLE process, void *remotePath, LPTHREAD_START_ROUTINE loadLibrary,
                           HMODULE &remoteClientModule, bool &pathSafeToFree, std::wstring &error)
{
    pathSafeToFree = false;
    UniqueHandle remoteThread(CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr));
    if (!remoteThread.valid())
    {
        error = WindowsFailure(L"CreateRemoteThread failed", GetLastError());
        return false;
    }
    if (!WaitForThread(remoteThread.get(), kInjectionTimeoutMilliseconds, L"Timed out while loading the client DLL.",
                       L"Waiting for the injection thread failed.", error))
        return false;
    pathSafeToFree = true;
    DWORD remoteResult = 0;
    if (!GetExitCodeThread(remoteThread.get(), &remoteResult) || remoteResult == 0)
    {
        error = L"LoadLibraryW failed inside the game process.";
        return false;
    }
    remoteClientModule = reinterpret_cast<HMODULE>(static_cast<ULONG_PTR>(remoteResult));
    return true;
}

bool ResolveExportRva(const std::filesystem::path &clientDll, const char *exportName, std::uintptr_t &entryRva,
                      std::wstring &error)
{
    HMODULE localImage = LoadLibraryExW(clientDll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (localImage == nullptr)
    {
        error = WindowsFailure(L"Could not inspect the client exports", GetLastError());
        return false;
    }
    FARPROC localEntry = GetProcAddress(localImage, exportName);
    const auto localBase = reinterpret_cast<std::uintptr_t>(localImage);
    const auto localAddress = reinterpret_cast<std::uintptr_t>(localEntry);
    entryRva = localEntry != nullptr && localAddress >= localBase ? localAddress - localBase : 0;
    // Resolve the RVA locally, then call the already-loaded remote image.
    FreeLibrary(localImage);
    if (entryRva == 0)
    {
        error = L"The client DLL does not export the required startup entry point.";
        return false;
    }
    return true;
}

bool InvokeRemoteExport(HANDLE process, HMODULE remoteClientModule, std::uintptr_t entryRva, void *parameter,
                        DWORD timeoutMilliseconds, DWORD expectedResult, DWORD &result, std::wstring &error)
{
    auto remoteAddress =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<std::uintptr_t>(remoteClientModule) + entryRva);
    UniqueHandle initializationThread(CreateRemoteThread(process, nullptr, 0, remoteAddress, parameter, 0, nullptr));
    if (!initializationThread.valid())
    {
        error = WindowsFailure(L"CreateRemoteThread for client initialization failed", GetLastError());
        return false;
    }
    if (!WaitForThread(initializationThread.get(), timeoutMilliseconds, L"Timed out while initializing the client DLL.",
                       L"Waiting for client initialization failed.", error))
        return false;
    if (!GetExitCodeThread(initializationThread.get(), &result))
    {
        error = WindowsFailure(L"Could not read the client startup result", GetLastError());
        return false;
    }
    if (result == expectedResult)
        return true;
    wchar_t detail[96] = {};
    swprintf_s(detail, L"The client startup entry point returned 0x%08lX; expected 0x%08lX.",
               static_cast<unsigned long>(result), static_cast<unsigned long>(expectedResult));
    error = detail;
    return false;
}
} // namespace

bool InjectClient(HANDLE process, const std::filesystem::path &clientDll, HMODULE &remoteClientModule,
                  std::wstring &error)
{
    const std::wstring dllPath = clientDll.wstring();
    // LoadLibraryW executes in the target process, so its path must be copied
    // into target-owned memory before creating the remote thread.
    SIZE_T bytes = 0;
    void *remotePath = AllocateRemotePath(process, dllPath, bytes, error);
    if (remotePath == nullptr)
        return false;
    if (!WriteRemotePath(process, remotePath, dllPath, bytes, error))
    {
        ReleaseRemotePath(process, remotePath);
        return false;
    }
    LPTHREAD_START_ROUTINE loadLibrary = ResolveLoadLibrary(error);
    if (loadLibrary == nullptr)
    {
        ReleaseRemotePath(process, remotePath);
        return false;
    }
    bool pathSafeToFree = false;
    const bool loaded =
        LoadClientIntoProcess(process, remotePath, loadLibrary, remoteClientModule, pathSafeToFree, error);
    if (pathSafeToFree)
    {
        ReleaseRemotePath(process, remotePath);
    }
    // A timed-out LoadLibraryW thread may still read the path. The caller
    // terminates the suspended process on failure, reclaiming this allocation.
    return loaded;
}

static bool InvokeInjectedClientExport(HANDLE process, HMODULE remoteClientModule,
                                       const std::filesystem::path &clientDll, const char *exportName, void *parameter,
                                       DWORD timeoutMilliseconds, DWORD expectedResult, DWORD &result,
                                       std::wstring &error)
{
    if (process == nullptr || remoteClientModule == nullptr)
    {
        error = L"The injected client module handle was invalid.";
        return false;
    }
    std::uintptr_t entryRva = 0;
    if (!ResolveExportRva(clientDll, exportName, entryRva, error))
        return false;
    return InvokeRemoteExport(process, remoteClientModule, entryRva, parameter, timeoutMilliseconds, expectedResult,
                              result, error);
}

bool InitializeInjectedClient(HANDLE process, HMODULE remoteClientModule, const std::filesystem::path &clientDll,
                              std::wstring &error)
{
    DWORD result = 0;
    return InvokeInjectedClientExport(process, remoteClientModule, clientDll, "AlbionTogetherInitialize", nullptr,
                                      kInjectionTimeoutMilliseconds, kClientPreResumeReady, result, error);
}

bool WaitForInjectedClientReady(HANDLE process, HMODULE remoteClientModule, const std::filesystem::path &clientDll,
                                std::wstring &error)
{
    DWORD result = 0;
    return InvokeInjectedClientExport(
        process, remoteClientModule, clientDll, "AlbionTogetherWaitForReady",
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(kRuntimeReadyTimeoutMilliseconds)),
        kRuntimeReadyTimeoutMilliseconds + 5'000, kClientRuntimeReady, result, error);
}
} // namespace fable::launcher::runtime
