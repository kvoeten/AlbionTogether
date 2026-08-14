#include "CreateFileFunctions.h"

#include <cstring>

namespace fable::game::definitions::native
{
    bool CreateFileFunctions::Resolve(Resolved& resolved) noexcept
    {
        resolved = {};
        const HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
        if (kernelBase == nullptr)
        {
            return false;
        }

        const auto wideFunction = reinterpret_cast<WideFunction>(
            GetProcAddress(kernelBase, "CreateFileW"));
        const auto ansiFunction = reinterpret_cast<AnsiFunction>(
            GetProcAddress(kernelBase, "CreateFileA"));
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto nativeFunction = ntdll == nullptr
            ? nullptr
            : reinterpret_cast<NativeFunction>(
                GetProcAddress(ntdll, "NtCreateFile"));
        const auto nativeOpenFunction = ntdll == nullptr
            ? nullptr
            : reinterpret_cast<NativeOpenFunction>(
                GetProcAddress(ntdll, "NtOpenFile"));
        if (wideFunction == nullptr || ansiFunction == nullptr ||
            nativeFunction == nullptr || nativeOpenFunction == nullptr)
        {
            return false;
        }

        bool valid = false;
        __try
        {
            const auto* const nativeBytes = reinterpret_cast<const std::uint8_t*>(
                nativeFunction);
            const auto* const nativeOpenBytes = reinterpret_cast<const std::uint8_t*>(
                nativeOpenFunction);
            valid = std::memcmp(
                        reinterpret_cast<const void*>(wideFunction),
                        ExpectedPrologue.data(),
                        ExpectedPrologue.size()) == 0 &&
                std::memcmp(
                        reinterpret_cast<const void*>(ansiFunction),
                        ExpectedPrologue.data(),
                        ExpectedPrologue.size()) == 0 &&
                nativeBytes[0] == 0xB8 && nativeBytes[5] == 0xBA &&
                nativeOpenBytes[0] == 0xB8 && nativeOpenBytes[5] == 0xBA;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }

        resolved.wideFunction = wideFunction;
        resolved.ansiFunction = ansiFunction;
        resolved.nativeFunction = nativeFunction;
        resolved.nativeOpenFunction = nativeOpenFunction;
        return true;
    }
}
