#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat::native
{
    struct CombatHealthMutationFunction final
    {
        using Pointer = void(__thiscall*)(void*, float, bool);

        // CThingCreature::ModifyCombatHealth is the shared implementation
        // reached by player, guard, and other creature health mutations. The
        // player override at RVA 0x01B5A520 only rounds its delta before
        // delegating here and therefore cannot observe NPC damage directly.
        static constexpr std::uintptr_t AddressRva = 0x01B59CB0;
        static constexpr std::uintptr_t ExceptionHandlerRva = 0x0250CE58;
        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::array<std::uint8_t, 3> ExpectedPrefix = {
            0x6A, 0xFF, 0x68,
        };

        static bool Resolve(HMODULE gameModule, std::uint8_t*& address) noexcept;
    };
}
