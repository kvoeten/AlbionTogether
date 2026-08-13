#pragma once

#include <Windows.h>
#include <winternl.h>

#include <array>
#include <cstdint>

namespace fable::game::definitions::native
{
    struct CreateFileFunctions final
    {
        using WideFunction = HANDLE(WINAPI*)(
            LPCWSTR fileName,
            DWORD desiredAccess,
            DWORD shareMode,
            LPSECURITY_ATTRIBUTES securityAttributes,
            DWORD creationDisposition,
            DWORD flagsAndAttributes,
            HANDLE templateFile);
        using AnsiFunction = HANDLE(WINAPI*)(
            LPCSTR fileName,
            DWORD desiredAccess,
            DWORD shareMode,
            LPSECURITY_ATTRIBUTES securityAttributes,
            DWORD creationDisposition,
            DWORD flagsAndAttributes,
            HANDLE templateFile);
        using NativeFunction = NTSTATUS(NTAPI*)(
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
        using NativeOpenFunction = NTSTATUS(NTAPI*)(
            PHANDLE fileHandle,
            ACCESS_MASK desiredAccess,
            POBJECT_ATTRIBUTES objectAttributes,
            PIO_STATUS_BLOCK ioStatusBlock,
            ULONG shareAccess,
            ULONG openOptions);

        static constexpr std::size_t PrologueSize = 5;
        static constexpr std::array<std::uint8_t, PrologueSize>
            ExpectedPrologue = {0x8B, 0xFF, 0x55, 0x8B, 0xEC};

        struct Resolved final
        {
            WideFunction wideFunction = nullptr;
            AnsiFunction ansiFunction = nullptr;
            NativeFunction nativeFunction = nullptr;
            NativeOpenFunction nativeOpenFunction = nullptr;
        };

        static bool Resolve(Resolved& resolved) noexcept;
    };
}
