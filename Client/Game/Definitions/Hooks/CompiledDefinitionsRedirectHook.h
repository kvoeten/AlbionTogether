#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Definitions/Native/CreateFileFunctions.h"

#include <Windows.h>

#include <atomic>
#include <array>
#include <string>

namespace fable::game::definitions
{
    class CompiledDefinitionsRedirectHook final
    {
    public:
        bool InstallEarly(
            HMODULE gameModule,
            const wchar_t* gameDefinitionsPath) noexcept;
        void Shutdown() noexcept;

        void Report(const core::Diagnostics& diagnostics);

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static HANDLE WINAPI CreateFileWide(
            LPCWSTR fileName,
            DWORD desiredAccess,
            DWORD shareMode,
            LPSECURITY_ATTRIBUTES securityAttributes,
            DWORD creationDisposition,
            DWORD flagsAndAttributes,
            HANDLE templateFile);
        static HANDLE WINAPI CreateFileAnsi(
            LPCSTR fileName,
            DWORD desiredAccess,
            DWORD shareMode,
            LPSECURITY_ATTRIBUTES securityAttributes,
            DWORD creationDisposition,
            DWORD flagsAndAttributes,
            HANDLE templateFile);
        static NTSTATUS NTAPI CreateFileNative(
            PHANDLE fileHandle,
            ACCESS_MASK desiredAccess,
            POBJECT_ATTRIBUTES objectAttributes,
            PIO_STATUS_BLOCK ioStatusBlock,
            PLARGE_INTEGER allocationSize,
            ULONG fileAttributes,
            ULONG shareAccess,
            ULONG createDisposition,
            ULONG createOptions,
            PVOID eaBuffer,
            ULONG eaLength);
        static NTSTATUS NTAPI OpenFileNative(
            PHANDLE fileHandle,
            ACCESS_MASK desiredAccess,
            POBJECT_ATTRIBUTES objectAttributes,
            PIO_STATUS_BLOCK ioStatusBlock,
            ULONG shareAccess,
            ULONG openOptions);

        static CompiledDefinitionsRedirectHook* active_;

        native::CreateFileFunctions::WideFunction originalWide_ = nullptr;
        native::CreateFileFunctions::AnsiFunction originalAnsi_ = nullptr;
        native::CreateFileFunctions::NativeFunction originalNative_ = nullptr;
        native::CreateFileFunctions::NativeOpenFunction originalNativeOpen_ = nullptr;
        void* trampolineMemory_ = nullptr;
        std::array<void*, 4> targets_ = {};
        std::wstring gameDefinitionsPath_;
        std::wstring gameDefinitionsNtPath_;
        std::string gameDefinitionsPathAnsi_;
        std::atomic_bool redirectOccurred_{false};
        std::atomic_bool readyReported_{false};
        std::atomic_bool redirectReported_{false};
        core::Diagnostics diagnostics_ = {};
        bool installed_ = false;
    };
}
