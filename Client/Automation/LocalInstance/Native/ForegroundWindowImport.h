#pragma once

#include <Windows.h>

#include <cstdint>

namespace fable::automation::local_instance::native
{
    struct ForegroundWindowImport final
    {
        using Function = HWND(WINAPI*)();

        // Fable Anniversary retail Win32 SHA-256:
        // 2a95eea3c2cce9b47ca0f454a605b6952216f5d25158efd12ba48b70130989f2
        static constexpr std::uintptr_t SlotRva = 0x0265BB74;

        struct Resolved final
        {
            Function* slot = nullptr;
            Function importedFunction = nullptr;
        };

        static bool Resolve(HMODULE gameModule, Resolved& resolved) noexcept;
    };
}
