#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::player::input::native
{
    struct PlayerCommandPollFunction final
    {
        using Pointer = bool(__thiscall*)(
            void* gamePlayerInterface,
            void* outputCommand);

        static constexpr std::uintptr_t VtableRva = 0x02AE338C;
        static constexpr std::size_t VtableSlot = 1;
        static constexpr std::uintptr_t FunctionRva = 0x01885870;

        [[nodiscard]] static bool Resolve(
            HMODULE gameModule,
            void*** slot,
            Pointer& function) noexcept;
    };
}
