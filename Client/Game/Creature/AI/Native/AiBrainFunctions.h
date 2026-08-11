#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::ai::native
{
    struct AiBrainFunctions final
    {
        using UpdatePointer = void(__thiscall*)(void* brain);

        static constexpr std::uintptr_t VtableRva = 0x02AAF388;
        static constexpr std::uintptr_t UpdateRva = 0x016D7700;
        static constexpr std::size_t UpdateSlot = 4;

        static constexpr std::size_t FiberOffset = 0x44;
        static constexpr std::size_t DefinitionOffset = 0x9C;
        static constexpr std::size_t ContextBeginOffset = 0x1C;
        static constexpr std::size_t FiberPausedOffset = 0x10;
        static constexpr std::size_t FiberDispatchSlot = 12;

        static bool ResolveUpdateSlot(
            HMODULE gameModule,
            void*** slot,
            UpdatePointer& function) noexcept;
    };
}
