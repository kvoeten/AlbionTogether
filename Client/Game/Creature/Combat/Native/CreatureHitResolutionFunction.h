#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat::native
{
    struct CreatureHitResolutionFunction final
    {
        using Pointer = void(__thiscall*)(void* creature, void* hitParameters);

        // CThingCreatureBase::OnHit in the current 32-bit retail build.
        static constexpr std::uintptr_t AddressRva = 0x01B43EC0;
        static constexpr std::uintptr_t ExceptionHandlerRva = 0x02553E59;
        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::array<std::uint8_t, 3> ExpectedPrefix = {
            0x6A, 0xFF, 0x68,
        };

        static bool Resolve(HMODULE gameModule, std::uint8_t*& address) noexcept;
    };
}
