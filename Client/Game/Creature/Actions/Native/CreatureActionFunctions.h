#pragma once

#include "Game/Creature/Equipment/CreatureWeaponFamily.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::actions::native
{
    struct CreatureActionFunctions final
    {
        using SubmitPointer = bool(__thiscall*)(void* creature, void* action);
        using FinishPointer = void(__thiscall*)(void* action);
        using UpdatePointer = void(__thiscall*)(void* creature);
        using ImmediateAttackConstructorPointer = void* (__thiscall*)(
            void* action,
            void* attacker,
            void* target,
            std::int32_t actionTime,
            const void* actionContext);
        using UntargetedAttackConstructorPointer = void* (__thiscall*)(
            void* action,
            void* attacker,
            void* target,
            const float* targetPosition,
            const void* actionContext);
        using ActionDeletingDestructorPointer = void* (__thiscall*)(
            void* action,
            unsigned int flags);
        using ActionDestructorPointer = void* (__thiscall*)(void* action);
        using WeaponTransitionConstructorPointer = void* (__thiscall*)(
            void* action,
            void* creature,
            std::int32_t mode);

        static constexpr std::uintptr_t UpdateAddressRva = 0x01B42E20;
        static constexpr std::uintptr_t UpdateExceptionHandlerRva = 0x02549BC8;
        static constexpr std::uintptr_t SubmitAddressRva = 0x01B42F70;
        static constexpr std::uintptr_t SubmitExceptionHandlerRva = 0x02553CB0;
        static constexpr std::uintptr_t FinishAddressRva = 0x017EF370;
        static constexpr std::uintptr_t FinishExceptionHandlerRva = 0x02512A56;
        // The shared InterruptableMidAttack constructor plus this action
        // vtable enters STRIKE_MEDIUM_FRONT when the action becomes active.
        // Do not use GameInterface::SetAttackImmediately's concrete wrapper
        // here: its begin method installs GENERIC_RESPONSE_DRAIN, which drives
        // an AI response mode but does not animate a remote presentation pawn.
        static constexpr std::uintptr_t ImmediateAttackConstructorAddressRva =
            0x017A70E0;
        static constexpr std::uintptr_t
            ImmediateAttackConstructorExceptionHandlerRva = 0x0250D9AC;
        static constexpr std::uintptr_t ImmediateAttackActionVtableRva =
            0x02AC08C4;
        static constexpr std::uintptr_t ImmediateAttackActionFirstMethodRva =
            0x01718A30;
        static constexpr std::int32_t ImmediateAttackActionTime = 0x96;
        // The Hero combat factory uses this constructor for an
        // InterruptableMidAttackAutoTurn action when no creature target is
        // available. It still takes an aim point, allowing the equipped
        // weapon's native swing, animation events, and VFX to run normally.
        static constexpr std::uintptr_t UntargetedAttackConstructorAddressRva =
            0x017B4160;
        static constexpr std::uintptr_t
            UntargetedAttackConstructorExceptionHandlerRva = 0x0250E64E;
        static constexpr std::uintptr_t UntargetedAttackActionVtableRva =
            0x02AC7E0C;
        static constexpr std::uintptr_t
            UntargetedAttackActionDeletingDestructorRva = 0x017B48F0;
        static constexpr std::uintptr_t ActionDestructorAddressRva =
            0x016E7950;
        static constexpr std::uintptr_t ActionDestructorExceptionHandlerRva =
            0x025016B6;
        // The public GameInterface wrapper performs Hero/AI policy checks
        // before constructing this ordinary creature action. Remote Heroes
        // deliberately combine an AI inventory with AHeroPawn presentation,
        // so submit the same native action directly through CThingCreature.
        // Modes: 1=sheathe, 4=draw melee, 5=draw ranged.
        static constexpr std::uintptr_t WeaponTransitionConstructorAddressRva =
            0x0179E1E0;
        static constexpr std::uintptr_t WeaponTransitionActionVtableRva =
            0x02AC37D4;
        static constexpr std::uintptr_t
            WeaponTransitionActionFirstMethodRva = 0x01718A30;
        static constexpr std::size_t WeaponTransitionStorageSize = 0x130;
        static constexpr std::array<std::uint8_t, 7>
            WeaponTransitionConstructorPrefix = {
                0x83, 0xEC, 0x08, 0x33, 0xC0, 0x56, 0x89,
            };
        static constexpr std::size_t ImmediateAttackStorageSize = 0x130;
        static constexpr std::size_t UntargetedAttackStorageSize = 0x144;
        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::array<std::uint8_t, 3> ExpectedPrefix = {
            0x6A, 0xFF, 0x68,
        };

        static bool ResolveUpdate(HMODULE gameModule, std::uint8_t*& address) noexcept;
        static bool ResolveSubmit(HMODULE gameModule, std::uint8_t*& address) noexcept;
        static bool ResolveFinish(HMODULE gameModule, std::uint8_t*& address) noexcept;
        static bool SubmitImmediateAttack(
            HMODULE gameModule,
            void* attacker,
            void* target) noexcept;
        static bool SubmitUntargetedAttack(
            HMODULE gameModule,
            void* attacker,
            const float (&targetPosition)[3]) noexcept;
        static bool SubmitWeaponTransition(
            HMODULE gameModule,
            void* creature,
            game::creature::equipment::CreatureWeaponFamily family) noexcept;

    private:
        static bool Resolve(
            HMODULE gameModule,
            std::uintptr_t addressRva,
            std::uintptr_t exceptionHandlerRva,
            std::uint8_t*& address) noexcept;
    };
}
