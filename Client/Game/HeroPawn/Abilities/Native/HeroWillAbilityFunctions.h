#pragma once

#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Abilities/HeroAbility.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::hero_pawn::abilities::native
{
    struct HeroWillAbilityFunctions final
    {
        using CommandPointer = bool (__thiscall*)(void*, HeroAbility);
        using TurncoatStatePointer = void (__thiscall*)(void*, float);
        using AbilityProgressionGetterPointer = int (__thiscall*)(void*, int);
        using AbilityProgressionSetterPointer =
            void (__thiscall*)(void*, int, int);

        // Anniversary CTCSpecialAbilities vtable, identified through its RTTI
        // complete-object locator. UseSpecialAbility is a non-virtual member.
        static constexpr std::uintptr_t ControllerVtableRva = 0x02B13D44;
        static constexpr std::uintptr_t AbilityInventoryVtableRva =
            0x02AFBF04;
        static constexpr std::uintptr_t UseRva = 0x01ACE5E0;
        static constexpr std::uintptr_t ToggleRva = 0x01ACC0A0;
        static constexpr std::uintptr_t CancelRva = 0x01ACC110;
        static constexpr std::uintptr_t EligibilityRva = 0x01ACBDC0;
        static constexpr std::uintptr_t TurncoatStateRva = 0x01B03D90;
        static constexpr std::uintptr_t AbilityProgressionGetterRva =
            0x019E6AA3;
        static constexpr std::uintptr_t AbilityProgressionSetterRva =
            0x019E6B1A;
        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::size_t TurncoatStateDisplacedBytes = 8;

        [[nodiscard]] static bool ResolveCommand(
            HMODULE gameModule,
            HeroAbilityCommand command,
            std::uint8_t*& address) noexcept;
        [[nodiscard]] static bool ResolveEligibility(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        [[nodiscard]] static bool ResolveTurncoatState(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        [[nodiscard]] static void* FindComponent(
            void* hero,
            HMODULE gameModule) noexcept;
        [[nodiscard]] static void* ReadOwner(void* component) noexcept;
        [[nodiscard]] static void* ReadCreature(void* component) noexcept;
        [[nodiscard]] static bool ReadAbilityProgressionState(
            void* component,
            HeroAbility ability,
            int& state) noexcept;
        [[nodiscard]] static bool ApplyAbilityProgressionState(
            void* component,
            HeroAbility ability,
            int state) noexcept;
        [[nodiscard]] static bool HasActiveAction(
            void* component,
            HeroAbility ability) noexcept;
    };
}
