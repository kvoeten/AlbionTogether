#pragma once

#include "Game/Native/ScriptTypes.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::locomotion::native
{
    struct FollowCreatureActionFunctions final
    {
        using FollowEntityPointer = void(__thiscall*)(
            void* scriptedControl,
            const game::native::ScriptThing* target,
            float distance,
            bool avoidDynamicObstacles);
        using ActionMethodPointer = void(__thiscall*)(void* action);

        static constexpr std::uintptr_t FollowEntityRva = 0x0190FA90;
        static constexpr std::uintptr_t ActionConstructorRva = 0x0190E630;
        static constexpr std::uintptr_t ActionEnqueueRva = 0x01AAF4E0;
        static constexpr std::uintptr_t ActionTickRva = 0x01AAF7B0;
        static constexpr std::uintptr_t ActionStartRva = 0x01AB0CE0;
        static constexpr std::uintptr_t ActionStopRva = 0x01AB0F70;
        static constexpr std::uintptr_t ActionVtableRva = 0x02AE9EEC;

        static constexpr std::size_t ActionSize = 0x28;
        static constexpr std::size_t ActionControllerOffset = 0x04;
        static constexpr std::size_t ActionTargetOffset = 0x0C;
        static constexpr std::size_t ActionDistanceOffset = 0x20;
        static constexpr std::size_t ActionAvoidDynamicObstaclesOffset = 0x25;
        static constexpr std::size_t DisplacedBytes = 7;

        static constexpr std::array<std::uint8_t, 7> TickExpectedPrefix = {
            0x83, 0xEC, 0x5C, 0x53, 0x55, 0x56, 0x57,
        };
        static constexpr std::array<std::uint8_t, 3> StartExpectedPrefix = {
            0x6A, 0xFF, 0x68,
        };

        static bool ResolveTick(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
        static bool ResolveStart(
            HMODULE gameModule,
            std::uint8_t*& address) noexcept;
    };
}
