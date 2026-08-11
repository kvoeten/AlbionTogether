#pragma once

#include <Windows.h>

#include <cstdint>

namespace fable::core::game_thread::native
{
    struct PeekMessageImport final
    {
        using Function = BOOL(WINAPI*)(
            LPMSG message,
            HWND window,
            UINT minimumMessage,
            UINT maximumMessage,
            UINT removeMessage);

        static constexpr std::uintptr_t SlotRva = 0x0265BB14;

        struct Resolved final
        {
            Function* slot = nullptr;
            Function importedFunction = nullptr;
        };

        static bool Resolve(HMODULE gameModule, Resolved& resolved) noexcept;
    };
}
